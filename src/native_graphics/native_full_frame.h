#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace edf::native {
struct NativeScenePublication;
struct NativeScenePassCamera;
struct NativeScenePassAnimation;
// Full-frame native renderer (edf_native_full_frame). The render helper hook
// sub_821A5080 runs one NativeFullFrame instead of the guest helper and
// instead of DispatchNativeFrame's guest callbacks: no guest function is
// called during a native frame.
//
// Where the frame sits in the engine loop 821A6508 (clSgsCoreRender vtable
// 82001EF0, owner+132): slot +24 821BEA00 presents the previous frame
// (8219C1F8 -> 82151460 -> edf_native_swap_wait, which submits the scene
// recorder and paces), slot +0 821BE868 runs scene setup 8219C7A8 (its hook
// allocates the native scene targets, clears them and makes the renderer the
// active scene), then 821A53A8 joins the render helper 821A5920 -> 821A5080
// (this frame), then slot +20 821BE9F0 ends the frame in 8219C840, whose hook
// publishes the scene to the presentation queue. A native frame therefore
// records into the scene 8219C7A8 opened and leaves it for 8219C840: with no
// native post output it takes the direct route (scene color resolved to the
// frame buffer owner+124+index*4 and published), as the loading path does.
//
// Contract:
// - Run(host) calls, in order: AcquireInputs, Views, then per view
//   AdvanceSerial, BeginView (skips the view's passes when false), every pass
//   in order through RunPass, EndView; then SideEffects and EndScene once.
// - Views is empty when the helper would skip its view loop (owner bytes
//   2261/2262 set, or neither 2216 nor 2217); the frame still ends.
// - A pass never calls guest code. A stub pass reports Unimplemented once per
//   Record; the host decides how often to log.
// - Exceptions propagate to the hook; EndScene is not called after one.
struct NativeFrameInputs {
  std::shared_ptr<const NativeScenePublication> publication;
  std::shared_ptr<const std::map<uint32_t,NativeScenePassCamera>> cameras;
  std::shared_ptr<const std::map<uint32_t,NativeScenePassAnimation>> animations;
  uint64_t motion_publication=0;  // Model motion generation (native_render_publication).
};
struct NativeFrameView {
  uint32_t scene=0,index=0;
  uint32_t serial=0;  // owner+136 before this view's increment.
};
// Guest viewport 821BE8D0 would set from scene+480, and the native one bound.
struct NativeFrameViewport {
  uint32_t x=0,y=0,width=0,height=0;
  float min_depth=0,max_depth=1;
};
class NativeFrameHost;
struct NativeFrameContext {
  NativeFrameHost& host;
  const NativeFrameInputs& inputs;
  NativeFrameView view;
  NativeFrameViewport viewport;
  uint32_t renderer=0;       // Active native scene owner (Word(8257BFB4)).
  bool output_ready=false;   // Set by a post pass that produced the ordinary output.
};
struct NativeFramePass {
  virtual ~NativeFramePass()=default;
  virtual const char* name()=0;
  virtual void Record(NativeFrameContext&)=0;
};
class NativeFrameHost {
 public:
  virtual ~NativeFrameHost()=default;
  virtual NativeFrameInputs AcquireInputs()=0;
  virtual std::vector<uint32_t> Views()=0;
  // Returns owner+136 and stores it plus one, as DispatchNativeFrame does.
  virtual uint32_t AdvanceSerial()=0;
  virtual bool BeginView(NativeFrameContext&)=0;
  virtual void RunPass(size_t index,NativeFramePass& pass,NativeFrameContext& context) { (void)index; pass.Record(context); }
  virtual void EndView(NativeFrameContext&) {}
  // HOOK POINT: the helper's guest side effects other code relies on, from
  // the ongoing side-effect research. Called once per frame before EndScene.
  virtual void SideEffects(const NativeFrameInputs&) {}
  virtual void EndScene(const NativeFrameInputs&,bool output_ready)=0;
  virtual void Unimplemented(const char* pass) { (void)pass; }
};
// Stable pass order; timing phases frame.native.<name> follow it.
inline constexpr std::string_view kNativeFramePassOrder[]{
  "static_world","models","sky","effects","transparent","post"};
class NativeFullFrame {
 public:
  NativeFullFrame();  // The default ordered stub passes.
  explicit NativeFullFrame(std::vector<std::unique_ptr<NativeFramePass>> passes):passes_(std::move(passes)) {}
  void Run(NativeFrameHost& host);
  std::span<const std::unique_ptr<NativeFramePass>> passes() const { return passes_; }
  uint64_t frames() const { return frames_; }
 private:
  std::vector<std::unique_ptr<NativeFramePass>> passes_;
  uint64_t frames_=0;
};
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFramePasses();

// Which path the render helper hook takes this call. full_frame needs the
// shader bridge and host; A/B guest-side frames take today's path so native
// and guest images come from alternating live frames.
enum class NativeFrameRoute : uint32_t { guest_helper,frame_dispatch,full_frame };
constexpr NativeFrameRoute SelectNativeFrameRoute(bool full_frame,bool frame_dispatch,bool shader_bridge,
                                                  bool host,bool ab_native_side) {
  if(full_frame && shader_bridge && host && ab_native_side) return NativeFrameRoute::full_frame;
  if(frame_dispatch && shader_bridge) return NativeFrameRoute::frame_dispatch;
  return NativeFrameRoute::guest_helper;
}
}
