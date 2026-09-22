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
struct NativeRenderRegistrySnapshot;
// Full-frame native renderer (edf_native_full_frame). The render helper hook
// sub_821A5080 runs one NativeFullFrame instead of the guest helper. It
// mirrors DispatchNativeFrame's structure (native_frame_dispatch.h): one view
// per frame in practice (no shadow or reflection views), then the finish
// stage, then the phase loop that draws the HUD/XUI onto the output surface.
//
// Where the frame sits in the engine loop 821A6508 (clSgsCoreRender vtable
// 82001EF0, owner+132): slot +24 821BEA00 presents the previous frame
// (8219C1F8 -> 82151460 -> edf_native_swap_wait, which submits the scene
// recorder and paces), slot +0 821BE868 runs scene setup 8219C7A8 (its hook
// allocates the native scene targets, clears them and makes the renderer the
// active scene), then 821A53A8 joins the render helper 821A5920 -> 821A5080
// (this frame), then slot +20 821BE9F0 ends the frame in 8219C840, whose hook
// publishes the active ordinary output (after post) or, when the scene is
// still open, resolves the scene to the frame buffer and publishes that.
//
// Contract of Run(host), in order:
// - AcquireInputs, Views. Views is empty when the helper skips its view loop
//   (owner bytes 2261/2262 set, or neither 2216 nor 2217); the finish stage
//   and phase loop still run, as in the guest.
// - Per view: AdvanceSerial(view); BeginView, and when it accepts, every view
//   pass in order through RunPass; ViewOverlays (always, as the guest runs its
//   per-view listeners); EndView.
// - SideEffects, then every frame pass (Post) through RunPass with a frame
//   context (view.scene 0); output_ready is what they report.
// - Phases (the HUD on the output target), then EndScene(output_ready).
// Pass indexes for RunPass follow kNativeFramePassOrder: view passes first.
// Passes never call guest code; the host's ViewOverlays, Finish fallback and
// Phases are the frame's remaining guest calls (kNativeFrameRemainingGuestCalls).
// Exceptions propagate to the hook; nothing after the throw runs.
struct NativeFrameInputs {
  std::shared_ptr<const NativeScenePublication> publication;
  std::shared_ptr<const std::map<uint32_t,NativeScenePassCamera>> cameras;
  std::shared_ptr<const std::map<uint32_t,NativeScenePassAnimation>> animations;
  uint64_t motion_publication=0;  // Model motion generation (native_render_publication).
  // The render registry's latest tick snapshot (native_render_entry.h); null before its first tick.
  std::shared_ptr<const NativeRenderRegistrySnapshot> registry;
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
  uint32_t renderer=0;       // Active native scene owner (Word(8257BFB4)); 0 when BeginView declined.
  bool output_ready=false;   // Set by the post pass: the ordinary output is active for 8219C840.
};
struct NativeFramePass {
  virtual ~NativeFramePass()=default;
  virtual const char* name() const=0;
  virtual void Record(NativeFrameContext&)=0;
};
class NativeFrameHost {
 public:
  virtual ~NativeFrameHost()=default;
  virtual NativeFrameInputs AcquireInputs()=0;
  virtual std::vector<uint32_t> Views()=0;
  // Returns owner+136 and stores it plus one, as DispatchNativeFrame does, and
  // fills the guest frame context the view's guest listeners read.
  virtual uint32_t AdvanceSerial(uint32_t view)=0;
  virtual bool BeginView(NativeFrameContext&)=0;
  virtual void RunPass(size_t index,NativeFramePass& pass,NativeFrameContext& context) { (void)index; pass.Record(context); }
  // The view's guest listeners (remaining guest calls).
  virtual void ViewOverlays(NativeFrameContext&) {}
  virtual void EndView(NativeFrameContext&) {}
  // HOOK POINT: the helper's guest side effects other code relies on, from
  // the ongoing side-effect research. Called once per frame before the post.
  virtual void SideEffects(const NativeFrameInputs&) {}
  // The finish stage 820B0B80 for the post pass: true when the ordinary output
  // is left active with this frame's image (native post, or its guest fallback).
  virtual bool Finish(NativeFrameContext&) { return false; }
  // The phase loop (owner+140..+144 x listener +16): the HUD, on the output.
  virtual void Phases(NativeFrameContext&) {}
  virtual void EndScene(const NativeFrameInputs&,bool output_ready)=0;
  virtual void Unimplemented(const char* pass) { (void)pass; }
};
// Stable pass order; timing phases frame.native.<name> follow it. The first
// kNativeFrameViewPassCount run per view, the rest once per frame.
inline constexpr std::string_view kNativeFramePassOrder[]{
  "static_world","models","sky","effects","transparent","post"};
inline constexpr size_t kNativeFrameViewPassCount=5;
// What a full frame still runs as guest code, until each is native.
inline constexpr std::string_view kNativeFrameRemainingGuestCalls[]{
  "view listeners owner+2232 +12 (clSatoCallback 8216DA80)",
  "view +16 (clPlayerCamera 820D3FD0: follow/talk icons, trajectory ribbon)",
  "finish stage 820B0B80, only when the native post reports an error",
  "phase loop owner+140..+144 x listener +16 (HUD: clNoguchiCallback 820A4DD0, clSatoCallback 8216E630)"};
class NativeFullFrame {
 public:
  NativeFullFrame();  // The default ordered passes (stubs but for Post).
  NativeFullFrame(std::vector<std::unique_ptr<NativeFramePass>> view_passes,
                  std::vector<std::unique_ptr<NativeFramePass>> frame_passes)
    :view_passes_(std::move(view_passes)),frame_passes_(std::move(frame_passes)) {}
  void Run(NativeFrameHost& host);
  std::span<const std::unique_ptr<NativeFramePass>> view_passes() const { return view_passes_; }
  std::span<const std::unique_ptr<NativeFramePass>> frame_passes() const { return frame_passes_; }
  // Replaces the pass named `name` (either list); false when there is none.
  bool Replace(std::unique_ptr<NativeFramePass> pass);
  uint64_t frames() const { return frames_; }
 private:
  std::vector<std::unique_ptr<NativeFramePass>> view_passes_,frame_passes_;
  uint64_t frames_=0;
};
// Stubs for the view passes, in order.
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFramePasses();
// The frame passes: Post, which records through NativeFrameHost::Finish.
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFrameFinishPasses();

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
