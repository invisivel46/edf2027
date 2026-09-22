#pragma once
// Full-frame renderer: what the renderable registry publishes per simulation
// tick, and what the native frame consumes. The registry is fed by the base
// render-object constructor 821C2090 / destructor 821C1FE8, the update
// subscription 821C0D70 and the end of 821A4DE8; nothing here is read from the
// render walk (sub_821A5080 does not run in full-frame mode).
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
};
// One LOD choice: the object-relative model instance address and its layout.
struct NativeRenderModel {
  uint32_t instance=0;          // guest address of the cl3D9_Model instance
  std::shared_ptr<const NativeModelLayout> layout;
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
};
struct NativeRenderRegistrySnapshot {
  uint64_t tick=0;
  std::vector<std::shared_ptr<const NativeRenderEntry>> entries; // dense, unordered
};
}
