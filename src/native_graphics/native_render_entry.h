#pragma once
// Full-frame renderer: what the renderable registry publishes per simulation
// tick, and what the native frame consumes. The registry is fed by the base
// render-object constructor 821C2090 / destructor 821C1FE8, the update
// subscription 821C0D70 and the end of 821A4DE8; nothing here is read from the
// render walk (sub_821A5080 does not run in full-frame mode).
#include "native_shared_vector.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace edf::native {
struct NativeModelLayout;   // native_model_publication.h
enum class NativeRenderPoseCadence : uint8_t { Constructed, Tick, Frame };
enum class NativeRenderLodKind : uint8_t { None, FieldParts, Character };
// Per-class facts, keyed by vtable: where the model instance(s) and pose
// vector live and how the pose is produced.
struct NativeRenderClass {
  uint32_t vtable=0;
  const char* name="";
  NativeRenderPoseCadence cadence=NativeRenderPoseCadence::Tick;
  NativeRenderLodKind lod=NativeRenderLodKind::None;
  uint32_t instance=0,pose=0;   // object offsets; 0 when the class draws no model
  bool effect=false;            // dynamic geometry built at render time (effects)
  // Extra models the class draws beside its LOD choice (NativeRenderAttachmentBit).
  uint8_t attachments=0;
  // Tracked but not snapshotted: its draws come from the existing scene sources.
  bool scene_source=false;
  // Frame cadence posed by the registry: the object offset of the root matrix
  // slot 4 passes to 821C8C58 with the tree at instance+16 before 821C9478
  // fills the pose vector (native_model_hierarchy.h). 0: not posed natively
  // (clSky: the sky pass poses it from the rendered camera).
  uint32_t frame_root=0;
  // Tracked but not snapshotted: another full-frame pass draws it (clSky: the
  // sky pass poses it from the camera and its static node tree). Its pose
  // vector +384 is written only by its own slot 4 820BB270, which never runs in
  // full-frame mode, so a snapshot would hold whatever pose the last guest
  // render left: the models pass would draw the dome a second time with it.
  bool other_pass=false;
};
// NativeRenderClass::attachments bits. Face: 820DB268 draws obj+1588 with pose
// obj+1636 when byte obj+1584 is set. Weapons: 820DE790 walks obj+1824 (count
// +1832, stride 1408) when obj+1768 is zero; 820E1A80 draws w+100 with pose
// w+144 when bytes w+1405 and w+1404 are set, w+108 is nonnull and not
// (w+412==2 && w+804==0). MotherSpheres: 820EC180 draws obj+1172 through
// 821C9DA8 once per 20-byte record at *(obj+1220) (count obj+1228), each with
// its own world (native_render_instances.h); published as `instanced`.
enum NativeRenderAttachmentBit : uint8_t { kNativeRenderFace=1,kNativeRenderWeapons=2,kNativeRenderMotherSpheres=4 };
// One LOD choice: the object-relative model instance address and its layout.
struct NativeRenderModel {
  uint32_t instance=0;          // guest address of the cl3D9_Model instance
  std::shared_ptr<const NativeModelLayout> layout; // null until the pose is sized and the model decodes
};
using NativeRenderPose=std::shared_ptr<const std::vector<std::array<float,16>>>;
// Where a published pose came from, for interpolation in unlocked mode
// (native_render_motion.h, AdvanceNativeRenderPoseMotion): previous is the
// same matrices at the tick before `tick`, when the registry read them then
// and nothing reset in between; null when the pose must not blend (first
// read, a skipped tick or step jump, a new generation, a bone count or layout
// change, or a render-dependent pose). tick: the tick this pose was first
// read at. render_dependent: the pose changed between two reads of one tick
// (as NativeModelPoseHistory), sticky until a reset.
struct NativeRenderPoseMotion {
  NativeRenderPose previous;
  uint64_t tick=0;
  bool render_dependent=false;
  bool operator==(const NativeRenderPoseMotion&) const=default;
};
// A model drawn with its own pose vector (face, weapons), in guest draw order.
struct NativeRenderAttachment {
  NativeRenderModel model;
  uint32_t pose_vector=0;       // guest address of the attachment's pose vector
  NativeRenderPose pose;
  NativeRenderPoseMotion motion;
};
// A model drawn once per world through 821C9DA8, in guest draw order: the
// layout is decoded without a pose vector (NativeModelLayout::single_world)
// and worlds[i] is the one g_mWorld every drawn record of instance i uses.
// The worlds are computed natively at tick time from the object's fields.
struct NativeRenderInstanced {
  NativeRenderModel model;
  NativeRenderPose worlds;
  NativeRenderPoseMotion motion; // Of `worlds`, as a pose.
};
// Immutable per-tick copy of what the frame needs from one render object.
struct NativeRenderEntry {
  uint32_t object=0;
  uint64_t generation=0;        // registry lifetime token (address reuse safe)
  const NativeRenderClass* type=nullptr;
  // Visibility inputs (820B4038 / 821C0C00): bound centre obj+288 (x,y,z,w),
  // radius obj+352, cull distance obj+76, sort mode obj+52, sort bias obj+56,
  // hidden halfword obj+64.
  std::array<float,4> centre{};
  // The rest of the 821B2B00 bound after the centre: three oriented half axes
  // obj+304/+320/+336 (float4 each), the box 821C33E8 tests on a partial sphere.
  std::array<float,12> axes{};
  float radius=0,cull_distance=0,sort_bias=0;
  int32_t mode=0;
  bool hidden=false;
  // LOD thresholds (8210AE48: 48-byte records at *(obj+1128); 820B2670:
  // obj+448+44i) and the model chosen for each threshold; models[0] is the
  // default (e.g. obj+1168 for characters).
  std::vector<float> lod_thresholds;
  std::vector<NativeRenderModel> models;
  // Pose vector snapshot: 64-byte row-major matrices as the guest stores them.
  std::shared_ptr<const std::vector<std::array<float,16>>> pose;
  uint32_t pose_vector=0;       // guest address of the pose vector (obj+type->pose)
  NativeRenderPoseMotion motion;
  // Face and weapons (NativeRenderAttachmentBit), drawn after the LOD model in
  // this order, as 820DEA08 does: 820DB268 (8210AE48, then the face), then
  // 820DE790 unless 820DBBD0.
  std::vector<NativeRenderAttachment> attachments;
  std::vector<NativeRenderInstanced> instanced;  // Drawn after the model, as slot 4 does.
};
struct NativeRenderRegistrySnapshot {
  uint64_t tick=0;
  // Dense, ordered by object. Shared chunks like `objects`: a publication
  // copies it in O(1) and a changed entry clones one chunk, not every pointer.
  NativeSharedVector<std::shared_ptr<const NativeRenderEntry>> entries;
  // Registry publication counter, and the same entries keyed by object. The
  // map shares every chunk an older snapshot did not see change, and an entry
  // pointer is carried over unchanged when its object did not change: pointer
  // equality between two snapshots means "same entry".
  uint64_t generation=0;
  NativeSharedMap<uint32_t,std::shared_ptr<const NativeRenderEntry>> objects;
};
}
