#pragma once
// FSR 3.1 native anti-aliasing (edf_native_fsr=native_aa) on the full-frame
// scene: the upscaler context, its jitter sequence, the camera and reset
// inputs it needs, and the dispatch recorded at the end of the scene.
//
// Where it sits in a full frame (guest_shader_bridge.cpp, NativeFullFrameHost):
//   BeginView   - this frame's jitter (NativeFsrUpscaler::Prepare, then the
//                 Halton offset of the frame index). The PASS camera stays
//                 unjittered: it keys the static world's whole-frame reuse,
//                 per-group camera constants and camera plan, the culls, the
//                 effects eye, the guest view globals and (workstream B) the
//                 motion vectors. The jitter goes only where camera constants
//                 are written into draws: NativeSceneRenderer::SetClipJitter
//                 (sky, models, static world, transparent model batches) and
//                 a jittered copy of the camera for the effect activations.
//   RunPass     - after "static_world" (the last opaque pass): the scene
//                 colour copied as the opaque-only colour, the reactive
//                 mask's reference.
//   ViewOverlays- the guest listeners draw with the view globals, so these
//                 are rewritten jittered around the call and put back after.
//   MotionVectors (workstream B, native_motion_vectors.h) - recorded for an
//                 FSR view even with edf_native_motion_vectors off, from the
//                 unjittered pass camera; EndView ends the view's jitter.
//   Finish      - the frame context's motion (the last view's) goes to FSR.
//   ResolveScene- (the native post's mode-1 resolve, or the 8219C930 hook's)
//                 after the resolve: GENERATEREACTIVEMASK, then the upscale,
//                 recorded with RecordRaw, and owner+104 (the post's HDR
//                 input) mapped to the output texture. Post and HUD never see
//                 the jitter.
//
// Nothing here runs with edf_native_fsr=off: the renderer's jitter stays
// (0, 0), which records exactly the pre-FSR bytes, and no pass, copy or
// dispatch is added.
#include "native_render_backend.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Workstream B's per-view motion vector output (native_motion_vectors.h).
// Without that header (native_fsr.h built on its own), the same shape here.
#if __has_include("native_motion_vectors.h")
#include "native_motion_vectors.h"
#define EDF_NATIVE_FSR_MOTION_VECTORS 1
#else
#define EDF_NATIVE_FSR_MOTION_VECTORS 0
namespace edf::native {
struct NativeMotionVectorOutput {
  NativeBackendTexture* motion=nullptr;
  bool reset=true;
  std::array<float,16> view_projection{};
  float near_plane=0,far_plane=0,fov_y=0;
};
}  // namespace edf::native
#endif

namespace edf::native {
class NativeFfxLibrary;

// edf_native_fsr. Every FSR quality mode is read, so the settings screen and
// configs can name them now; only native_aa (1.0x) is implemented, and the
// others run as native_aa until render scaling exists (NativeFsrEffectiveMode).
enum class NativeFsrMode : uint32_t { Off, NativeAA, Quality, Balanced, Performance, UltraPerformance };
std::optional<NativeFsrMode> ParseNativeFsrMode(std::string_view text);
std::string_view NativeFsrModeName(NativeFsrMode mode);
// FFX_UPSCALE_QUALITY_MODE_* for a mode (Off has none).
uint32_t NativeFsrQualityMode(NativeFsrMode mode);
constexpr NativeFsrMode NativeFsrEffectiveMode(NativeFsrMode mode) {
  return mode==NativeFsrMode::Off?NativeFsrMode::Off:NativeFsrMode::NativeAA;
}

// The validation tools assume deterministic, unjittered, same-size scenes
// (shadow-diff, A/B and reuse alternation compare pixels across routes), so
// FSR turns itself off while any of them is set. Null when none is.
struct NativeFsrExclusions { bool ab_alternate=false,reuse_off_alternate=false,shadow_render=false; };
const char* NativeFsrExcludedBy(const NativeFsrExclusions& exclusions);

// ---------------------------------------------------------------------------
// Jitter. FFX's sequence (ffxFsr3UpscalerGetJitterPhaseCount/Offset, which
// GETJITTERPHASECOUNT/GETJITTEROFFSET answer): 8 * (display/render)^2 phases,
// offset i = (Halton(i % n + 1, 2) - 0.5, Halton(i % n + 1, 3) - 0.5) pixels.
// The local copies are the fallback when the query fails and what the tests
// hold the DLL's answers against.
int32_t NativeFsrJitterPhaseCount(uint32_t render_width,uint32_t display_width);
float NativeFsrHalton(int32_t index,int32_t base);
std::array<float,2> NativeFsrJitterOffset(int32_t index,int32_t phase_count);
// One frame's jitter, in the FidelityFX sample's convention
// (fsrapirendermodule.cpp): with the queried offset q, the dispatch is told
// pixel = -q, and the projection moves NDC by clip = (2 pixel.x / w,
// -2 pixel.y / h) (NDC y is up, pixels are down).
struct NativeFsrJitter {
  float pixel_x=0,pixel_y=0;  // ffxDispatchDescUpscale::jitterOffset
  float clip_x=0,clip_y=0;    // NDC offset folded into the projection
  bool active() const { return clip_x!=0 || clip_y!=0; }
};
NativeFsrJitter MakeNativeFsrJitter(std::array<float,2> queried,uint32_t render_width,uint32_t render_height);
// NativeSceneClipJitter on guest matrix words (row vectors, each word a
// float's bits, as NativeScenePassCamera holds them): column 0 += x column 3,
// column 1 += y column 3. For the projection and view*projection; the view is
// never jittered.
std::array<uint32_t,16> NativeFsrJitterGuestMatrix(const std::array<uint32_t,16>& matrix,float clip_x,float clip_y);
// A pass camera (NativeScenePassCamera: projection, view, view_projection as
// guest words) with the jitter in its projection and view*projection.
template<class Camera> Camera NativeFsrJitterCamera(const Camera& camera,const NativeFsrJitter& jitter) {
  Camera result=camera;
  result.projection=NativeFsrJitterGuestMatrix(camera.projection,jitter.clip_x,jitter.clip_y);
  result.view_projection=NativeFsrJitterGuestMatrix(camera.view_projection,jitter.clip_x,jitter.clip_y);
  return result;
}

// ---------------------------------------------------------------------------
// The camera FSR needs (near, far, vertical fov), from a row-vector D3D
// perspective projection: P[1][1] = cot(fov/2), P[2][2] = f/(f-n),
// P[3][2] = -n f/(f-n), P[2][3] = 1 (left-handed), or the right-handed form
// the game builds (821C82C0: P[2][2] = -f/(f-n), P[2][3] = -1). The scene is
// reversed-Z through the viewport depth range, not the matrix, so the matrix
// is a forward one; any other shape (orthographic, degenerate) keeps the
// defaults and derived=false.
//
// What the dispatch is given: cameraNear = n, cameraFar = f, finite, with
// the context's DEPTH_INVERTED and without DEPTH_INFINITE. FFX's
// setupDeviceDepthToViewSpaceDepthParams (ffx_fsr3upscaler.cpp) takes
// min/max of the two and lets the flags pick the transform, so their order
// does not matter; the FidelityFX sample's cameraNear = FLT_MAX, cameraFar =
// near is its infinite reversed projection, which this game does not use (a
// forward finite matrix through a [1, 0] viewport is exactly the finite
// reversed transform).
struct NativeFsrCameraParams {
  float near_plane=0.1f,far_plane=10000.f,fov_y=1.0f;
  bool derived=false;
};
// A projection change larger than these between two dispatched frames is a
// camera cut (history reset); smaller ones (a zoom) are reprojected.
inline constexpr float kNativeFsrCutFov=0.25f;        // radians, as NativeCameraHistory::CutWith
inline constexpr float kNativeFsrCutPlaneRatio=2.0f;  // near or far, either way
NativeFsrCameraParams NativeFsrCameraFromProjection(const std::array<float,16>& projection);
NativeFsrCameraParams NativeFsrCameraFromProjectionWords(const std::array<uint32_t,16>& projection);

// What the dispatch takes from workstream B's output, or the fallback without
// it: B's texture and reset, its camera where it gave one, else the pass
// camera's. No texture means zero motion and a reset, because accumulating
// with zero vectors while the camera moves smears the image.
struct NativeFsrMotionPlan {
  NativeBackendTexture* motion=nullptr;  // null: the upscaler's zero texture
  bool reset=true;
  NativeFsrCameraParams camera;
};
NativeFsrMotionPlan PlanNativeFsrMotion(const NativeMotionVectorOutput& output,const NativeFsrCameraParams& pass_camera);

// ---------------------------------------------------------------------------
// When the history must be dropped. Each frame FSR dispatches is described;
// the answer is the reasons (bits) this one resets for, 0 to accumulate.
enum NativeFsrReset : uint32_t {
  kNativeFsrResetFirst=1u<<0,    // no earlier frame (or after Forget)
  kNativeFsrResetResize=1u<<1,   // render or display size changed (context recreated)
  kNativeFsrResetFormat=1u<<2,   // colour format changed (context recreated)
  kNativeFsrResetRoute=1u<<3,    // a helper call in between did not dispatch (guest route, skipped view, direct frame)
  kNativeFsrResetAbSide=1u<<4,   // the A/B side changed
  kNativeFsrResetCut=1u<<5,      // the projection jumped (kNativeFsrCutFov, kNativeFsrCutPlaneRatio): a camera cut
  kNativeFsrResetMotion=1u<<6,   // the motion vectors asked for it (B's cut, or none)
  kNativeFsrResetMode=1u<<7,     // edf_native_fsr changed mode
};
struct NativeFsrFrameFacts {
  uint64_t helper_frame=0;      // the render helper call counter (one per 821A5080)
  uint32_t width=0,height=0,format=0;
  NativeFsrMode mode=NativeFsrMode::NativeAA;
  bool ab_native=true;
  NativeFsrCameraParams camera;
  bool motion_reset=false;
};
class NativeFsrResetTracker {
 public:
  uint32_t Next(const NativeFsrFrameFacts& facts);
  void Forget() { last_.reset(); }
  static std::string Describe(uint32_t reasons);
 private:
  std::optional<NativeFsrFrameFacts> last_;
};

// ---------------------------------------------------------------------------
// FidelityFX messages (the context's fpMessage), kept until the bridge logs
// them: this library has no logger.
std::vector<std::string> DrainNativeFsrMessages();
// The process's FidelityFX DLL (NativeFfxLibrary::DefaultPath), loaded on
// first use.
NativeFfxLibrary& NativeFsrLibrary();

struct NativeFsrDispatchInputs {
  NativeBackendTexture* color=nullptr;       // render-size RGBA16F: the resolved scene, jittered
  NativeBackendRenderTarget* depth=nullptr;  // the sampled scene depth target (reversed Z)
  NativeBackendTexture* motion=nullptr;      // null: zero motion
  bool opaque=false;                         // opaque_texture() holds this frame's opaque-only colour
  NativeFsrJitter jitter;
  std::array<float,2> motion_scale{};        // motion vector units to render pixels
  float sharpness=0;                         // 0 disables RCAS
  float frame_ms=16.6667f;
  bool reset=true;
  NativeFsrCameraParams camera;
};
struct NativeFsrStats {
  uint64_t contexts=0,dispatches=0,reactive=0,resets=0,retired=0;
  uint64_t context_bytes=0;  // the current context's GPU memory (FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE), 0 if unknown
};
// One FSR upscaler context on a D3D12 scene backend (NativeD3D12RawAccess),
// native AA: render size = display size. Made lazily by Prepare and made
// again when the size or colour format changes; a replaced context is
// destroyed only after kRetireFrames further Prepares, by which time the GPU
// has finished the frames that used it (the backend's frame slots are far
// fewer).
class NativeFsrUpscaler {
 public:
  static constexpr uint32_t kRetireFrames=16;
  NativeFsrUpscaler();
  ~NativeFsrUpscaler();
  NativeFsrUpscaler(const NativeFsrUpscaler&)=delete;
  NativeFsrUpscaler& operator=(const NativeFsrUpscaler&)=delete;
  // Ready for a frame of this size and colour format on this backend: the
  // context, output, reactive, opaque-copy and zero-motion textures. False
  // with *error when it cannot be (no D3D12 raw access, no FFX DLL or
  // provider, a failed create). recreated() says this call made them.
  bool Prepare(NativeRenderBackend& backend,uint32_t width,uint32_t height,uint32_t color_format,std::string* error);
  // A provider id from NativeFfxLibrary::Versions (0: the DLL's default,
  // newest), used by the next context made (FFX's ffxOverrideVersion).
  void SetVersionOverride(uint64_t id) { version_override_=id; }
  bool recreated() const { return recreated_; }
  bool ready() const { return context_!=nullptr; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  // The context's jitter sequence (GETJITTERPHASECOUNT/GETJITTEROFFSET), or
  // the local copy when the context cannot answer.
  int32_t JitterPhaseCount() const { return phase_count_; }
  std::array<float,2> JitterOffset(int32_t index) const;
  bool jitter_from_ffx() const { return jitter_from_ffx_; }
  // Where the opaque-only colour is copied to (render size, colour format).
  NativeBackendTexture* opaque_texture() const { return opaque_.get(); }
  // The upscaled image (display size, RGBA16F, UAV), for the post.
  const std::shared_ptr<NativeBackendTexture>& output() const { return output_; }
  // GENERATEREACTIVEMASK (with inputs.opaque) then the upscale, recorded into
  // `recorder` at this point of its stream (RecordRaw). Throws on failure.
  void Dispatch(NativeRenderBackend& backend,NativeBackendRecorder& recorder,const NativeFsrDispatchInputs& inputs);
  const NativeFsrStats& stats() const { return stats_; }
  // The provider this context got, for the log.
  const std::string& provider() const { return provider_; }
 private:
  void Destroy(void* context);
  void Release();
  NativeRenderBackend* backend_=nullptr;
  void* context_=nullptr;  // ffxContext
  uint32_t width_=0,height_=0,format_=0;
  int32_t phase_count_=8;
  uint64_t version_override_=0;
  bool jitter_from_ffx_=false,recreated_=false;
  std::shared_ptr<NativeBackendTexture> output_;
  std::unique_ptr<NativeBackendTexture> reactive_,opaque_,zero_motion_;
  struct Retired {
    void* context=nullptr;
    uint32_t frames=0;
    std::shared_ptr<NativeBackendTexture> output;
    std::unique_ptr<NativeBackendTexture> reactive,opaque,zero_motion;
  };
  std::vector<Retired> retired_;
  NativeFsrStats stats_;
  std::string provider_;
};
}  // namespace edf::native
