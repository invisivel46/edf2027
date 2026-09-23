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
#include <string>
#include <vector>

namespace edf::native {
struct NativeModelLayout;   // native_model_publication.h
enum class NativeRenderPoseCadence : uint8_t { Constructed, Tick, Frame };
enum class NativeRenderLodKind : uint8_t { None, FieldParts, Character };
// A float4 a slot-4 body stores into a shared effect parameter just before a
// draw: 821A1730(*(8257C02C), *(base+handle), base+value) -> 821A16D8, which
// copies min(1, *(handle+8)) float4s to *(handle+0) and stores nothing for a
// null handle. The handle is what 821A20C0 returned for the parameter's name
// in the shared effect pool (the pool map node + 40), looked up once by the
// class constructor (e.g. 820FC6F0: g_Highlight -> +1296, g_Time -> +1300);
// every material whose global of that name the draw activates reads it. The
// value is sticky: a later draw of the same slot 4 that stores nothing sees it.
// scroll: the value is not an object float4 but (-x, 0, 0, 1) with x the
// float at base+value (821E7C50: g_Scroll from tread+60, 0.0 [820009A4],
// 1.0 [820008CC]). handle 0: unused.
struct NativeRenderConstantSource { uint32_t handle=0,value=0; bool scroll=false; };
// Extra models slot 4 draws at fixed object offsets after the LOD model:
// count records of `stride` bytes from obj+base; record r stores its
// constants, then draws r+instance with pose vector r+pose through 821C9C20.
//  clAlienTank01 820ECCC0: 820F01E8 on obj+1920+1488i, i<2 (g_Highlight
//    +1120 <- +1168, g_Time +1124 <- +1184; instance +108, pose +152);
//  C_Tank 821E5810: 821E7C50 on obj+4216 and obj+4308 (g_Scroll +88 from
//    +60; instance +0, pose +72).
struct NativeRenderPart {
  uint32_t base=0,count=0,stride=0,instance=0,pose=0;
  std::array<NativeRenderConstantSource,2> constants{};
};
// A vehicle weapon group drawn after the LOD model: for each element i of
// *(obj+group+array) (count *(obj+group+count), re-read per element), 1504
// bytes apart, 820E1A80 on element+64: the weapon draws w+100 with pose w+144
// when byte w+1404 is set, w+108 is nonnull and not (w+412==2 && w+804==0).
// Unlike 820DE790 (people) there is no w+1405 test and no 820DBBD0 gate.
//  C_VehicleBase/C_Bike/C_PowerLoader 8219A2D0 -> 82199DD8(obj+1824): +36/+44;
//  C_Helicopter 821E2250 -> 821E4E90(obj+1536), 821E4E90(obj+1580): +28/+36;
//  C_Tank 821E5810 -> 821E4E90(obj+2208): +28/+36.
struct NativeRenderWeaponGroup { uint32_t group=0,array=0,count=0; };
inline constexpr uint32_t kNativeRenderVehicleWeaponStride=1504,kNativeRenderVehicleWeaponOffset=64;
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
  // Per-object constants stored before the LOD draw (821A1730 in slot 4), in
  // guest order; the extra models after it: vehicle weapon groups, then parts
  // (no class has both a face or people weapons and either).
  std::array<NativeRenderConstantSource,2> constants{};
  std::array<NativeRenderWeaponGroup,2> weapon_groups{};
  NativeRenderPart parts{};
};
// Whether a class stores any per-object constant (its entries are re-read
// every tick: the values advance outside scene+100).
constexpr bool NativeRenderClassHasConstants(const NativeRenderClass& type) {
  for(const auto& source:type.constants) if(source.handle) return true;
  if(type.parts.count) for(const auto& source:type.parts.constants) if(source.handle) return true;
  return false;
}
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
// One shared effect parameter's value as a draw sees it: the pool name the
// handle's node carries (node+12, the 821A20C0 key) and the float4 821A16D8
// stores over the first register, as guest bytes (big-endian words), which is
// what a material global of that name reads from the pool value.
struct NativeRenderObjectConstant {
  std::string name;
  std::array<uint8_t,16> registers{};
  bool operator==(const NativeRenderObjectConstant&) const=default;
};
// Every per-object constant in effect at one draw, in first-store order; null
// when none. Shared while unchanged, so pointer equality is value equality.
using NativeRenderConstants=std::shared_ptr<const std::vector<NativeRenderObjectConstant>>;
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
  // The object's constants in effect at this draw: the entry's, then those of
  // the parts drawn up to and including this one (sticky, as the pool is).
  NativeRenderConstants constants;
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
  // 820DE790 unless 820DBBD0; then the vehicle weapon groups and the parts
  // (NativeRenderClass::weapon_groups, parts) in slot-4 order.
  std::vector<NativeRenderAttachment> attachments;
  std::vector<NativeRenderInstanced> instanced;  // Drawn after the model, as slot 4 does.
  // Per-object constants the LOD model (and the instanced sets) draw with
  // (NativeRenderClass::constants), read at the tick.
  NativeRenderConstants constants;
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
