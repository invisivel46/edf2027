#pragma once
#include "native_render_backend.h"
#include <array>
#include <cstdint>
#include <vector>

namespace edf::native {
// Motion vectors for FSR 3.1 (edf_native_motion_vectors, default off). The
// interface workstream C (jitter, FSR dispatch, reactive mask) consumes; the
// implementation is native_motion_vector_pass.h/.cpp.
//
// What one view's record makes, and what it hands on:
//  - camera reprojection: every pixel of the view's viewport, from the scene
//    depth (read through its SRV) and the previous and current UNJITTERED
//    pass cameras: current pixel -> its position last frame, in UV units of
//    the render size (FSR's convention with motionVectorScale = render size,
//    i.e. previous_uv = current_uv + motion, y down). Reversed-Z: depth 0 (the
//    clear, the sky) reprojects as a direction (rotation only).
//  - model velocity: the models pass's opaque items whose transforms changed
//    since the previous frame are drawn again with a small native vertex
//    shader per kind (rigid, skinned with the bone palette, instanced),
//    current and previous transforms, depth test GREATER_EQUAL against the
//    scene depth without writing it, overwriting the camera motion in their
//    pixels. See native_motion_vector_pass.h for its limits (cutout alpha).
// History is valid only when the scene rendered in the immediately previous
// frame with the same viewport and render size and the camera did not cut
// (NativeCameraHistory::CutWith: > 50 units, > 0.25 rad of fov or > 90
// degrees of rotation); otherwise the texture is all zero and `reset` is set.
struct NativeMotionVectorOutput {
  NativeBackendTexture* motion=nullptr;   // RG16F, UV-space current->previous, render size; null when disabled/unavailable
  bool reset=true;                         // history invalid this frame (cut, first frame, resize)
  std::array<float,16> view_projection{};  // current unjittered, for FSR camera params
  float near_plane=0, far_plane=0, fov_y=0;// from the current pass camera
};

struct NativeScenePassCamera;
struct NativeMotionVelocityDraw;
class NativeMotionVectors;

// The arguments of one view's record. Every pointer is required unless noted.
//  backend, recorder: the scene backend and its recorder, with a frame open
//    (the full-frame host holds the bridge locks around the call). The record
//    binds its own targets, viewport, pipelines, constants, streams and
//    textures and leaves them bound (a recorder's PopState does not rebind
//    targets on every backend): callers that cache what they last bound must
//    forget it and bind their targets again afterwards.
//  depth: the scene depth target, created `sampled` (texture() is its depth
//    plane SRV), single-sampled, holding this view's depth (reversed-Z,
//    cleared to 0), not bound anywhere else. With another (no SRV, MSAA) nothing is
//    recorded and the output's motion is null.
//  depth_format: its DSV format (DXGI code), which the velocity pipelines
//    declare.
//  width, height: the render size, the scene surface's; the motion texture's.
//  viewport: the view's viewport in pixels (the one its passes drew with).
//  scene: the guest scene object (NativeFrameView::scene), the history key.
//  frame: the full-frame index (NativeFrameInputs::frame); consecutive
//    renders of a scene are consecutive indexes.
//  camera: the view's pass camera WITHOUT jitter. Jitter must not reach it
//    (or the caller passes an unjittered copy): FSR wants motion without it.
//  velocity: the models pass's velocity draws for this view (may be null or
//    empty: camera motion only).
struct NativeMotionVectorContext {
  NativeRenderBackend* backend=nullptr;
  NativeBackendRecorder* recorder=nullptr;
  NativeBackendRenderTarget* depth=nullptr;
  uint32_t depth_format=0;
  uint32_t width=0,height=0;
  NativeBackendViewport viewport;
  uint32_t scene=0;
  uint64_t frame=0;
  const NativeScenePassCamera* camera=nullptr;
  const std::vector<NativeMotionVelocityDraw>* velocity=nullptr;
};
// Called by the full-frame host per view after the transparent pass +
// ViewOverlays, before post (NativeFrameHost::MotionVectors). `state` owns the
// motion texture, the per-scene camera history and the pipelines; the texture
// stays valid until the next record into the same state with another render
// size or backend. Throws on a backend error; declines (null motion, reset)
// on an unusable depth target.
NativeMotionVectorOutput RecordNativeMotionVectors(NativeMotionVectors& state,const NativeMotionVectorContext& context);
}  // namespace edf::native
