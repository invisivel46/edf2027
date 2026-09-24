#pragma once
#include "d3d11_mesh.h"
#include "native_motion_history.h"
#include "native_motion_vectors.h"
#include "native_render_backend.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace edf::native {
// The recording half of the motion vectors (native_motion_vectors.h).
//
// Model velocity. The models pass keeps, per item state, the constants the
// item was drawn with in an earlier frame (NativeFullFrameModelItemState::
// last_values/last_frame, swapped once per frame when the values move) and,
// with NativeFullFrameModelPass::velocity, lists every opaque draw of every
// item whose constants moved by value this frame and were drawn in the frame
// before (NativeFullFrameModelFrame::velocity). Each is drawn again here from
// its own retained geometry (vertex and index buffers, input layout) with a
// native vertex shader of its kind:
//   Rigid     - the record's g_mWorld (current and previous), as 821A17D8 uploads it;
//   Skinned   - g_mWorldArray (current and previous PackNativeBonePalette
//               palettes) over BLENDINDICES, four BLENDWEIGHTs (VS_Blend) or
//               Indices[0] alone (VS_SingleBlend), whichever the game's vertex
//               shader reads (NativeMotionSkinningOf);
//   Instanced - an instanced world (NativeRenderEntry::instanced), several
//               instances of one geometry per draw from a per-instance stream.
// into the motion texture, depth test GREATER_EQUAL against the scene depth
// (reversed-Z) without writing depth, with a relative depth bias toward the
// camera (kNativeMotionDepthBias) so the same surface passes against its own
// depth despite a different transform order.
//
// Limits:
//  - Cutout alpha: the pass has no pixel shader of the material, so it cannot
//    discard what the material's alpha test or clip discards. In the holes of
//    a cutout (foliage, fences, grilles, a mech's vents) the velocity draw is
//    still in front of what shows through, passes the depth test and writes
//    the moving model's motion over the background's. Fully transparent
//    items are not drawn (only the opaque list is).
//  - Vertex deformation other than the rigid world or the palette (a
//    material animating its vertices over g_Time, a billboard) is not
//    reproduced: such pixels get the transform's motion, or none where the
//    shifted surface fails the depth test.
//  - An item drawn for the first time, drawn under another LOD or item state,
//    or not drawn in the previous frame has no velocity (camera motion only).
enum class NativeMotionVelocityKind : uint8_t { Rigid, Skinned, Instanced };
struct NativeMotionVelocityDraw {
  NativeMotionVelocityKind kind=NativeMotionVelocityKind::Rigid;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  // Rigid, Instanced: the world (row vectors, the pose matrix: translation in
  // 12..14) this frame and last frame.
  std::array<float,16> world{},previous_world{};
  // Skinned: PackNativeBonePalette floats (bones*12: three float4 registers
  // per bone, a column-major float4x3) this frame and last frame, and whether
  // the game's vertex shader blends four weights (else Indices[0] alone).
  std::shared_ptr<const std::vector<float>> palette,previous_palette;
  bool weighted=true;
};
// Whether a game vertex shader skins, and with how many weights: its input
// signature reads BLENDINDICES (and BLENDWEIGHT). VS_SingleBlend declares the
// weights and never reads them.
struct NativeMotionSkinning { bool indices=false,weights=false; };
NativeMotionSkinning NativeMotionSkinningOf(const NativeShader& shader);
// The row-vector world a g_mWorld register image holds (NativeModelWorldRegisters:
// the transpose as big-endian words).
std::array<float,16> NativeMotionWorldOf(const std::array<uint8_t,64>& registers);

// Reversed depth is scaled by 1 + this toward the camera: about 0.024% of the
// view distance. Enough for the reordered transform (world then view-projection
// here, world, view, projection in the game's shaders).
inline constexpr float kNativeMotionDepthBias=1.f/4096.f;
inline constexpr uint32_t kNativeMotionFormat=34;  // DXGI_FORMAT_R16G16_FLOAT
inline constexpr uint32_t kNativeMotionPaletteBones=68;

class NativeMotionVectors {
 public:
  NativeMotionVectors();
  ~NativeMotionVectors();
  NativeMotionVectors(const NativeMotionVectors&)=delete;
  NativeMotionVectors& operator=(const NativeMotionVectors&)=delete;
  struct Statistics {
    uint64_t records=0,resets=0,declined=0,velocity_draws=0,velocity_instances=0,velocity_skipped=0;
    std::array<uint64_t,7> reasons{};  // By NativeMotionReset.
  };
  const Statistics& statistics() const { return stats_; }
  const NativeMotionHistory& history() const { return history_; }
  // Why the last record reset (None when it did not).
  NativeMotionReset last_reset() const { return last_reset_; }
  // The motion render target (RG16F, sampled); null before the first record.
  NativeBackendRenderTarget* target() const;
  // Debug view (edf_native_motion_vectors_debug): mode 1 the motion as colour
  // (grey is still, red/green the x/y motion, saturating at 8 pixels), mode 2
  // the history-valid mask (green where the previous position is on screen
  // and history is valid, red elsewhere), drawn over the whole of `output`
  // (any size: the motion is point-sampled at the scaled position). Records
  // nothing without a motion texture. `extent`, when set, is the part of the
  // motion target the frame used (FSR upscaling's render-size corner).
  void RecordDebug(NativeRenderBackend& backend,NativeBackendRecorder& recorder,NativeBackendRenderTarget& output,
    uint32_t output_format,int mode,const NativeMotionVectorOutput& motion,std::array<uint32_t,2> extent={});
  // Forget every scene's history (the next record of each resets).
  void Reset() { history_.Reset(); }

 private:
  friend NativeMotionVectorOutput RecordNativeMotionVectors(NativeMotionVectors&,const NativeMotionVectorContext&);
  struct Resources;
  void Bind(NativeRenderBackend& backend);
  NativeBackendPipeline& CameraPipeline();
  NativeBackendPipeline* VelocityPipeline(NativeMotionVelocityKind kind,const NativeMotionVelocityDraw& draw,uint32_t depth_format,
    bool& weighted);
  void RecordVelocity(NativeBackendRecorder& recorder,const std::vector<NativeMotionVelocityDraw>& draws,
    const NativeMotionCamera& current,const NativeMotionCamera& previous,uint32_t depth_format,
    const NativeBackendViewport& viewport);
  Resources* resources_=nullptr;  // Of backend_; leaked when the backend changes (it may already be gone).
  NativeRenderBackend* backend_=nullptr;
  NativeMotionHistory history_;
  NativeMotionReset last_reset_=NativeMotionReset::First;
  Statistics stats_;
};
}  // namespace edf::native
