#pragma once
#include "native_bucket_dispatch.h"
#include "native_full_frame_base_state.h"
#include "native_model_pass.h"
#include "native_render_entry.h"
#include "native_scene_adapter.h"
#include "native_scene_visibility.h"
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace edf::native {
// The full-frame renderer's Models pass (kNativeFramePassOrder "models", and
// the model half of "transparent"): every model the renderable registry
// published, drawn from the registry snapshot and the pass camera alone. It
// replaces, for registry objects, end to end:
//   820B4038 per-object gather -> ClassifyNativeFullFrameModel (visibility)
//   821C0C00 route + key       -> mode 0 opaque list, modes 1/2 bucket key
//   821A3BA0 bucket traversal  -> the transparent list, key descending
//   8210AE48 / 820B2670 LOD    -> SelectNativeFullFrameModelLod
//   821C9C20 model draw        -> the layout's records/batches/passes with the
//                                 snapshot pose (821A17D8 rigid world,
//                                 821A1738 bone palette)
//   821C9DA8 world draw        -> one item per published instanced world
//                                 (NativeRenderEntry::instanced), every drawn
//                                 record with that world; identical draws of
//                                 the instances share one material and are
//                                 adjacent in the opaque order, so the scene
//                                 renderer draws them instanced
// No guest function is called, no device state is read or handed off and no
// guest-mirror eligibility is assessed. Each draw's render state is the pass
// base state plus its own material's state operations (see
// NativeFullFrameModelBaseState), never state chained from an earlier draw.

// The camera of one view. The published NativeScenePassCamera carries only the
// material matrices; visibility needs the float view matrix (camera+96, the
// same words as pass.view), the frustum (camera+288, 26 floats) and the depth
// scale (camera+400, which 821A5080 copies to context+8), and the modes-1
// bucket key needs context+0/+4 (Word(8201711C)/Word(82017120), which 821A5080
// stores per view). Until the camera publication carries them the frame host
// fills this from its view (ReadNativeSceneVisibilityView's fields).
struct NativeFullFrameModelCamera {
  NativeSceneVisibilityView visibility;
  NativeScenePassCamera pass;
  std::optional<NativeScenePassAnimation> animation;
  float key_scale=0,key_offset=0;
};

// 820B4038 / 821C0C00 for one entry, in the order the frame evaluates them.
enum class NativeFullFrameModelCull : uint8_t { Visible, Hidden, Mode, Distance, Frustum, Box };
struct NativeFullFrameModelVisibility {
  NativeFullFrameModelCull cull=NativeFullFrameModelCull::Visible;
  std::array<float,4> centre{};  // View-space centre (what the guest stores at context+32).
  float depth=0;                 // d=-float(z*depth_scale), the LOD and cull distance.
  uint32_t sphere=0,box=0;       // 821C3070 / 821C33E8 results (box only after a partial sphere).
  explicit operator bool() const { return cull==NativeFullFrameModelCull::Visible; }
};
// Hidden (obj+64) and an unsupported sort mode (not 0/1/2) reject first; then
// the centre is transformed by the view matrix, d > cull distance culls, and
// the sphere test with the radius classifies; a partial sphere (2) runs the
// box test on centre+axes, which rejects only when every corner is outside one
// plane. Boundary comparisons are NativeVisibilitySphere/Box's (821C3070/821C33E8).
NativeFullFrameModelVisibility ClassifyNativeFullFrameModel(const NativeRenderEntry& entry,const NativeSceneVisibilityView& view);
// The model index into entry.models, stateless, or none when the choice has
// no model. None: models[0]. Character (8210AE48): thresholds[i] selects
// models[i+1]; the last i with d > thresholds[i] wins, else models[0].
// FieldParts (820B2670, NativeVisibilityLod): thresholds[i] is LOD record i
// (obj+448+44i) and selects models[i]; the last i >= 1 with d > thresholds[i]
// wins, else models[0]; thresholds[0] is never compared.
std::optional<uint32_t> SelectNativeFullFrameModelLod(const NativeRenderEntry& entry,float depth);
// ComputeNativeBucketKey from values: mode 1 float(fma(z,scale,offset))*bias,
// mode 2 bias*65536, clamped to [0,65535] and converted as fctidz (NaN -> 0).
// z is the view-space centre z (context+40). Throws for any other mode.
inline uint16_t NativeFullFrameModelKey(int32_t mode,float view_z,float sort_bias,float scale,float offset) {
  double depth;
  if(mode==1) {
    depth=double(float(std::fma(double(view_z),double(scale),double(offset))));
    depth=double(float(depth*double(sort_bias)));
  } else if(mode==2) depth=double(float(double(sort_bias)*65536.0));
  else throw std::runtime_error("native full-frame model key for an unsupported sort mode");
  if(depth<0.0) depth=0.0;
  else if(depth>65535.0) depth=65535.0;
  return uint16_t(uint64_t(NativeFctidz(depth)));
}

// One model the frame draws: the LOD model chosen, or with instanced >= 0 the
// world `world` of entry->instanced[instanced], and the entry's sort data
// (instances share their object's visibility, key and route, as 820EC180
// draws them inside the one slot-4 call). entry points into the snapshot,
// which must outlive the plan.
struct NativeFullFrameModelItem {
  const NativeRenderEntry* entry=nullptr;
  uint32_t model=0;
  float depth=0,view_z=0;
  uint16_t key=0;
  bool transparent=false;
  int32_t instanced=-1;
  uint32_t world=0;
};
inline const NativeModelLayout& NativeFullFrameModelItemLayout(const NativeFullFrameModelItem& item) {
  return item.instanced<0?*item.entry->models[item.model].layout:*item.entry->instanced[size_t(item.instanced)].model.layout;
}
struct NativeFullFrameModelPlan {
  struct Stats {
    uint64_t entries=0,hidden=0,mode=0,distance=0,frustum=0,box=0,no_model=0,no_pose=0,bucket_zero=0,opaque=0,transparent=0,
      instances=0,no_instanced=0;
  };
  std::vector<NativeFullFrameModelItem> opaque;       // Snapshot order.
  std::vector<NativeFullFrameModelItem> transparent;  // Draw order: key descending, ties in snapshot order.
  Stats stats;
};
// Visibility, LOD and routing for every entry: its posed LOD model, then each
// instanced world in record order. Transparents follow 821A3BA0:
// buckets by high key byte 255 down to 1, each by low byte descending, equal
// keys in gather order. High bucket 0 (key < 256) is never traversed there,
// so those entries are dropped (stats.bucket_zero). The registry snapshot is
// unordered, so snapshot order stands in for the guest's gather order.
NativeFullFrameModelPlan PlanNativeFullFrameModels(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera);

// The per-object constants 821C9C20 uploads, from the snapshot pose.
//  worlds[mesh]: the g_mWorld registers each record draws with
//    (NativeModelWorldRegisters: the NativeRigidWorld transpose as big-endian
//    words). A rigid layout uploads pose[rec+44] for every record; a skinned
//    one only for records with rec+48 clear, and any other record sees the last
//    uploaded bone or, before any, the identity (the guest would see the
//    previous object's world; nothing native can reproduce that).
//  palette: skinned only, PackNativeBonePalette(pose,limit): bones*12 floats.
struct NativeFullFrameModelConstants {
  std::vector<std::array<uint8_t,64>> worlds;
  std::vector<float> palette;
  uint32_t bones=0;
  bool skinned=false;
};
NativeFullFrameModelConstants NativeFullFrameModelConstantsFor(const NativeModelLayout& layout,
  std::span<const NativePoseMatrix> pose,uint32_t palette_limit);
// 821C9DA8's constants: every record uploads the one world (records with
// rec+48 set are not drawn and keep no batches in a single-world layout).
NativeFullFrameModelConstants NativeFullFrameModelInstancedConstants(const NativeModelLayout& layout,const NativePoseMatrix& world);
// Replaces every vertex global g_mWorldArray's registers with the palette over
// zeroed registers of the same extent (the shader's reflected capacity).
// False when one is not a vertex global or cannot hold the palette.
bool BindNativeFullFrameModelPalette(std::vector<NativeSceneMaterialInputs::Constant>& constants,std::span<const float> palette);

// One draw of one item: record, batch and pass of the item's layout;
// index is its position in NativeModelDrawPlan, pass_index its position in the
// batch's pass list.
struct NativeFullFrameModelDrawRef {
  uint32_t item=0,index=0,pass_index=0;
  NativeModelDraw draw;
};
// Draw order of a list. Transparent: items in list order, each item's draws in
// guest order (record, batch, pass). Opaque: stably ordered by (pass index,
// batch, pass record), so draws of the same model resource and material are
// adjacent across entries and the renderer instances them, while every entry
// still draws its pass n before its pass n+1. Opaque records of one entry are
// thus not drawn in record order; depth testing makes that unobservable
// except for blended passes of mode-0 objects.
std::vector<NativeFullFrameModelDrawRef> OrderNativeFullFrameModelDraws(std::span<const NativeFullFrameModelItem> items,bool opaque);

// Target formats and depth direction of the pass: the definition every
// full-frame scene pass shares (native_full_frame_base_state.h).
using NativeFullFrameModelTargets=NativeFullFramePassTargets;
struct NativeFullFrameModelPass {
  NativeFullFrameModelTargets targets;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  int filtering=-1;
  // Word(descriptor+16) of the 821A1738 descriptor *(*(8257C02C)+36): the
  // runtime palette clamp, read once per frame by the host.
  uint32_t palette_limit=kNativeBonePaletteShaderBones;
};
// The explicit base state every model draw starts from, opaque and transparent
// alike: the shared full-frame base state (NativeFullFrameBaseState, the same
// operations as the static world and sky passes). A draw's state is this plus
// its own material's state operations; a transparent material's blend,
// depth-write and alpha operations are among those, so it keeps them.
inline constexpr const auto& kNativeFullFrameModelBaseOperations=kNativeFullFrameBaseOperations;
inline NativeSceneMaterialPassState NativeFullFrameModelBaseState(const NativeFullFrameModelTargets& targets) {
  return NativeFullFrameBaseState(targets);
}

// Published per-pass-record inputs. Neither may call guest code. program is
// the model pass program cache (Bridge::model_pass_loads, built by
// BuildNativeSceneMaterialLocked): the program and constant values of one
// 112-byte pass record. geometry is the retained batch geometry under the
// pass's vertex shader (Bridge::model_geometry_loads). intern is the adapter's
// InternMaterial. Missing results skip the whole entry. exclusive, when set,
// runs each material resolve (the backend's pipeline and sampler caches) and
// its intern in one call: the bridge wraps it in a short hold of its locks,
// so Build itself runs off them. program and geometry take their own holds.
struct NativeFullFrameModelSources {
  std::function<std::shared_ptr<const NativeSceneGroupMaterial>(uint32_t pass)> program;
  std::function<std::shared_ptr<const NativeIndexedMesh::RetainedDraw>(const NativeModelBatchLayout&,uint32_t pass)> geometry;
  std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)> intern;
  std::function<void(const std::function<void()>&)> exclusive;
};
// One NativeSceneRenderer::Render call: adjacent draws sharing a view. The
// snapshot holds the objects (and so geometry and materials) until the frame
// is dropped, which must not happen before the recording is submitted.
struct NativeFullFrameModelBatch {
  NativeSceneView view;
  NativeSceneSnapshot snapshot;
  bool transparent=false;
  // Transparent batches only (one item each): the item's bucket key and its
  // filing order among the plan's transparents (its index there), for
  // MergeNativeTransparentItems with the other producers.
  uint16_t key=0;
  uint32_t order=0;
};
struct NativeFullFrameModelFrame {
  struct Stats {
    uint64_t items=0,drawn=0,draws=0,resolves=0,memo_hits=0,missing_program=0,missing_geometry=0,
      scissor=0,palette=0,failed=0;
  };
  NativeFullFrameModelPlan plan;
  std::vector<NativeFullFrameModelBatch> batches;  // Opaque, then transparent.
  Stats stats;
};
// Cross-frame state: object ids only. Materials are interned through the
// sources; rigid ones are also memoized within a frame by (pass record,
// program, geometry), so identical entries share one material object and
// instance. Skinned materials carry their palette and never share. Not
// synchronized.
class NativeFullFrameModels {
 public:
  // Everything is resolved before anything is recorded. An entry any of whose
  // draws cannot be resolved is skipped whole (no partial objects).
  NativeFullFrameModelFrame Build(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera,
    const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources);
  // The caller has bound the pass's render targets (the frame host's BeginView).
  static NativeSceneRenderStatistics Record(NativeRenderBackend& backend,NativeSceneRenderer& renderer,
    const NativeFullFrameModelFrame& frame);
 private:
  uint64_t next_id_=(uint64_t(5)<<60);
};
// Build then Record. Returns the frame, which the caller keeps until submission.
NativeFullFrameModelFrame RecordNativeModels(NativeFullFrameModels& models,const NativeRenderRegistrySnapshot& snapshot,
  const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
  NativeRenderBackend& backend,NativeSceneRenderer& renderer,NativeSceneRenderStatistics* statistics=nullptr);
}
