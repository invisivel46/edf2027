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
};
// NativeRenderClass::attachments bits. Face: 820DB268 draws obj+1588 with pose
// obj+1636 when byte obj+1584 is set. Weapons: 820DE790 walks obj+1824 (count
// +1832, stride 1408) when obj+1768 is zero; 820E1A80 draws w+100 with pose
// w+144 when bytes w+1405 and w+1404 are set, w+108 is nonnull and not
// (w+412==2 && w+804==0).
enum NativeRenderAttachmentBit : uint8_t { kNativeRenderFace=1,kNativeRenderWeapons=2 };
// One LOD choice: the object-relative model instance address and its layout.
struct NativeRenderModel {
  uint32_t instance=0;          // guest address of the cl3D9_Model instance
  std::shared_ptr<const NativeModelLayout> layout; // null until the pose is sized and the model decodes
};
using NativeRenderPose=std::shared_ptr<const std::vector<std::array<float,16>>>;
// A model drawn with its own pose vector (face, weapons), in guest draw order.
struct NativeRenderAttachment {
  NativeRenderModel model;
  uint32_t pose_vector=0;       // guest address of the attachment's pose vector
  NativeRenderPose pose;
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
  std::vector<NativeRenderAttachment> attachments;
};
struct NativeRenderRegistrySnapshot {
  uint64_t tick=0;
  std::vector<std::shared_ptr<const NativeRenderEntry>> entries; // dense, unordered
  // Registry publication counter, and the same entries keyed by object. The
  // map shares every chunk an older snapshot did not see change, and an entry
  // pointer is carried over unchanged when its object did not change: pointer
  // equality between two snapshots means "same entry".
  uint64_t generation=0;
  NativeSharedMap<uint32_t,std::shared_ptr<const NativeRenderEntry>> objects;
};
}
