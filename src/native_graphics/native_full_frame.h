#pragma once
#include "native_frame_motion.h"
#include "native_motion_vectors.h"
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
//   per-view listeners); MotionVectors when BeginView accepted; EndView.
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
  // The motion budget of this render (tick, fraction, steps, whether model
  // poses interpolate): the one the camera's 821CDDF8 interpolation used.
  NativeFrameMotion motion;
  // Whether this render advances the state the guest advances once per render
  // (NativeTickGate over `motion`): NativeFullFrame::Run sets it once per
  // frame, before any view. Always true locked; unlocked, true only on the
  // first render after a simulation step. The effects pass commits
  // clEffectEtc02's +612 and the post advances its tone history only then;
  // the host's guest HUD phases step the radar shake and cursor fade only
  // then (the bridge's 82176708/8218ED68 hooks).
  bool tick_frame=true;
  // This render's index: NativeFullFrame::Run's count of frames, set once per
  // frame before any view (the motion vectors' history key: consecutive
  // renders have consecutive indexes).
  uint64_t frame=0;
};
struct NativeFrameView {
  uint32_t scene=0,index=0;
  uint32_t serial=0;  // owner+136 before this view's increment.
};
// Guest viewport 821BE8D0 would set from scene+488..+500 (x, y, width, height), and the native one bound.
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
  uint32_t owner=0;          // The helper's owner (r3 of 821A5080), set by BeginView.
  uint32_t guest_context=0;  // The guest frame context (helper stack+80) as the view's walks read it, set by BeginView.
  bool output_ready=false;   // Set by the post pass: the ordinary output is active for 8219C840.
  // Motion vectors (edf_native_motion_vectors, native_motion_vectors.h): in a
  // view's context, what NativeFrameHost::MotionVectors recorded for it (after
  // its passes and ViewOverlays); in the frame context the frame passes
  // (Post) and Phases get, the last accepted view's whose motion texture is
  // set, else the default (motion null, reset). For FSR (workstream C): the
  // texture, whether history reset, and the unjittered camera parameters.
  NativeMotionVectorOutput motion;
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
  // Motion vectors for an accepted view, after ViewOverlays and before
  // EndView: RecordNativeMotionVectors into context.motion. Records nothing
  // (and leaves the default) while edf_native_motion_vectors is off.
  virtual void MotionVectors(NativeFrameContext&) {}
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
// Stable pass order; timing phases frame.native.<name> follow it. The sky dome
// goes first, as the guest's map-effect walk draws it before the world: drawn
// after, its depth-tested dome covered everything beyond its radius. "sky" is
// that whole walk (clMapEffectManager's list in order: the sky and the
// electric wires' strips; native_map_effects.h). The models come before the
// static world, as the world-list walk (owner+44) draws them: the object
// managers' slot 2s gather and draw their mode-0 objects, then the map object
// manager's (820B4310) its list and then the octree, whose cells are the
// static world. Order is visible there: mode-0 materials with alpha-blended,
// depth-writing passes (the trees' and hedges' leaves) blend over what is
// already drawn and then hide what the static world would draw behind them.
// Objects the octree gathers (NativeFullFrameModelGather::unlisted), which the
// guest draws inside that walk, are drawn with the rest. The first
// kNativeFrameViewPassCount run per view, the rest once per frame.
inline constexpr std::string_view kNativeFramePassOrder[]{
  "sky","models","static_world","effects","transparent","post"};
inline constexpr size_t kNativeFrameViewPassCount=5;
// What a full frame still runs as guest code, until each is native.
inline constexpr std::string_view kNativeFrameRemainingGuestCalls[]{
  "view listeners owner+2232 +12 (clSatoCallback 8216DA80)",
  "view +16 (clPlayerCamera 820D3FD0: follow/talk icons, trajectory ribbon)",
  "finish stage 820B0B80, only when the native post reports an error",
  "phase loop owner+140..+144 x listener +16 (HUD: clNoguchiCallback 820A4DD0, clSatoCallback 8216E630)",
  "output bind after the native post: 8219C930 (target owner+112, depth owner+120, owner+96, untiled end) and 82135530(device,0)"};
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
  // Frames whose inputs.tick_frame was false (unlocked render-only frames).
  uint64_t held_frames() const { return held_frames_; }
 private:
  std::vector<std::unique_ptr<NativeFramePass>> view_passes_,frame_passes_;
  NativeTickGate tick_gate_;
  uint64_t frames_=0,held_frames_=0;
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

// The ordinary-output identity the per-draw hook 821FD8F8 requires of a HUD
// draw outside any target or scene (movie, font, XUI textured and Utility
// quads alike): the bridge's active output is the renderer with no target or
// scene open, and the guest device's bound color surface (device+12168, the
// SetRenderTarget 82137F98 mirror) is that output's surface owner+112. The
// bridge half alone is not enough: the native post sets it without any guest
// call, and until 8219C930 binds owner+112 the device still holds the scene's
// surface, so every HUD draw is refused as "unsupported ... output".
struct NativeOutputBinding {
  uint32_t active_output=0,active_target=0,active_scene=0;
  uint32_t output_surface=0;  // The native scene's output_surface (owner+112 once created).
  uint32_t device_surface=0;  // device+12168.
};
constexpr bool NativeOutputBound(uint32_t renderer,const NativeOutputBinding& binding) {
  return renderer && binding.active_output==renderer && !binding.active_target && !binding.active_scene &&
    binding.output_surface && binding.device_surface==binding.output_surface;
}
}
