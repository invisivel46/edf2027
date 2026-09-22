#pragma once
#include "native_model_publication.h"
#include "native_model_skinning.h"
#include <array>
#include <bit>
#include <optional>
#include <span>
#include <vector>

namespace edf::native {
// Why one 821C9C20 object ran the original model draw instead of the native
// model pass. Reasons before Program are decided from publications alone.
enum class NativeModelPassDecline : uint32_t {
  Unpublished, Stale, Skinned, PoseTick, Pose, RenderDependent, Bone, Buffers, Scene, Pending,
  Program, Scissor, Eligibility, Geometry, PassState, World, Palette, Count
};
inline constexpr std::array<const char*,size_t(NativeModelPassDecline::Count)> kNativeModelPassDeclineNames{
  "unpublished","stale","skinned","pose_tick","pose","render_dependent","bone","buffers","scene","pending",
  "program","scissor","eligibility","geometry","pass_state","world","palette"
};
struct NativeModelPassCounters {
  uint64_t objects=0,native=0,draws=0,passes=0,handoffs=0,replays=0;
  std::array<uint64_t,size_t(NativeModelPassDecline::Count)> fallbacks{};
  uint64_t Fallbacks() const { uint64_t sum=0; for(const auto count:fallbacks) sum+=count; return sum; }
};
// Publication gate of one object at 821C9C20 entry. rigid_byte is the live
// instance+12 byte; current is NativeModelPublications::Current for the live
// container/node/vector; tick is the render's published loop tick. skinned
// (edf_native_model_pass_skinned) admits the palette path: the live byte and
// the published layout must then both select it. A render-dependent pose
// declines on either path. Palette storage, its limit and the shader's array
// capacity need guest memory and native reflection: the render checks those.
struct NativeModelPoseGate {
  std::optional<NativeModelPassDecline> decline;
  const NativeModelPublications::Pose* pose=nullptr;
  explicit operator bool() const { return !decline; }
};
inline NativeModelPoseGate GateNativeModelPass(const NativeModelPublications::Layout& published,bool current,uint8_t rigid_byte,
    const NativeModelPublications::PosePublication* poses,uint64_t tick,bool render_dependent,bool skinned=false) {
  using D=NativeModelPassDecline;
  const auto decline=[](D reason) { return NativeModelPoseGate{reason,nullptr}; };
  if(!published) return decline(D::Unpublished);
  if(!current) return decline(D::Stale);
  const auto& layout=*published.layout;
  if((rigid_byte || layout.skinned) && (!skinned || !rigid_byte || !layout.skinned)) return decline(D::Skinned);
  if(!poses || poses->tick!=tick) return decline(D::PoseTick);
  const auto* pose=poses->Find(layout.instance);
  if(!pose || pose->layout_generation!=published.generation || pose->matrices.size()!=layout.bones) return decline(D::Pose);
  if(render_dependent) return decline(D::RenderDependent);
  // 821C9C20 traps on an empty palette source (decoding rejects it too).
  if(layout.skinned && pose->matrices.empty()) return decline(D::Pose);
  for(const auto& mesh:layout.meshes) {
    // Rigid instances upload every record's bone; palette records (rec+48!=0) none.
    if(mesh.uploads_bone?mesh.bone>=pose->matrices.size():!layout.skinned) return decline(D::Bone);
    for(const auto& batch:mesh.batches)
      if(!batch.vertex.generation || !batch.index.generation) return decline(D::Buffers);
  }
  return {std::nullopt,pose};
}
// One 821FE358 draw of the model path, in guest order: record, batch, pass.
struct NativeModelDraw {
  uint32_t mesh=0,batch=0,pass=0; // Mesh/batch indices into the layout; pass record address.
};
inline std::vector<NativeModelDraw> NativeModelDrawPlan(const NativeModelLayout& layout) {
  std::vector<NativeModelDraw> result;
  for(uint32_t mesh=0;mesh<layout.meshes.size();++mesh)
    for(uint32_t batch=0;batch<layout.meshes[mesh].batches.size();++batch)
      for(const auto pass:layout.meshes[mesh].batches[batch].passes) result.push_back({mesh,batch,pass});
  return result;
}
// 821A17D8 -> 821C8000 (transcribed in native_model_skinning.h): g_mWorld
// word c*4+r is pose row r, column c, i.e. the row-major pose stored
// column-major. As host words for guest stores, and as the big-endian
// register bytes the native scene world decoders read.
inline std::array<uint32_t,16> NativeModelWorldWords(const NativePoseMatrix& pose) {
  return std::bit_cast<std::array<uint32_t,16>>(NativeRigidWorld(pose));
}
inline std::array<uint8_t,64> NativeModelWorldRegisters(const NativePoseMatrix& pose) { return GuestRigidWorld(pose); }
// g_mWorld storage as each record's draws see it, by mesh index. A record that
// uploads its bone (every record of a rigid instance; rec+48==0 on the palette
// path) stores it through 821A17D8 before its 821B2C28; any other record draws
// with what the storage already holds: the last uploading record's bone, or
// entry (the storage bytes at 821C9C20 entry) when none came before.
inline std::vector<std::array<uint8_t,64>> NativeModelWorldPlan(const NativeModelLayout& layout,
    std::span<const NativePoseMatrix> pose,const std::array<uint8_t,64>& entry) {
  std::vector<std::array<uint8_t,64>> result;
  result.reserve(layout.meshes.size());
  auto current=entry;
  for(const auto& mesh:layout.meshes) {
    if(mesh.uploads_bone) {
      if(mesh.bone>=pose.size()) throw std::out_of_range("native model world bone out of range");
      current=NativeModelWorldRegisters(pose[mesh.bone]);
    }
    result.push_back(current);
  }
  return result;
}
// The record whose bone g_mWorld storage holds after the object, if any uploads.
inline std::optional<uint32_t> NativeModelLastWorldUpload(const NativeModelLayout& layout) {
  for(auto mesh=layout.meshes.size();mesh--;) if(layout.meshes[mesh].uploads_bone) return uint32_t(mesh);
  return std::nullopt;
}
// 821A1738 at 821C9D24 packs the whole pose, clamped to limit (Word(descriptor+16)),
// into the scratch at Word(descriptor+0) before any record draws; each pass
// then uploads g_mWorldArray from its global vector, which names that scratch.
// scratch is the live register bytes the shader's array covers (its reflected
// capacity, GuestFloatRegisterBytes). The result is those bytes with the
// PackNativeBonePalette bones written over their head as big-endian guest
// register words, which the constant bindings byte-swap back to the same host
// floats. Scratch past the clamped count is kept, as in the guest. No result
// when the packed palette does not fit the shader's array.
inline std::optional<std::vector<uint8_t>> NativeModelPaletteRegisters(std::span<const NativePoseMatrix> pose,uint32_t limit,
    std::span<const uint8_t> scratch) {
  if(pose.size()>0xffffffffull || scratch.size()%16) return std::nullopt;
  const auto count=NativeBonePaletteCount(uint32_t(pose.size()),limit);
  if(uint64_t(count)*kNativeBonePaletteBytes>scratch.size()) return std::nullopt;
  std::vector<float> floats(size_t(count)*kNativeBonePaletteFloats);
  PackNativeBonePalette(pose,floats,limit);
  std::vector<uint8_t> result(scratch.begin(),scratch.end());
  for(size_t i=0;i<floats.size();++i) StoreNativeGuestFloat(result.data()+i*4,floats[i]);
  return result;
}
// The same bones as the host words the device handoff stores at Word(descriptor+0),
// one 48-byte bone per entry: PackGuestBonePalette's bytes once stored big-endian.
inline std::vector<std::array<uint32_t,kNativeBonePaletteFloats>> NativeModelPaletteWords(
    std::span<const NativePoseMatrix> pose,uint32_t limit) {
  if(pose.size()>0xffffffffull) throw std::length_error("native bone palette source too large");
  std::vector<std::array<uint32_t,kNativeBonePaletteFloats>> result(NativeBonePaletteCount(uint32_t(pose.size()),limit));
  std::vector<float> floats(result.size()*kNativeBonePaletteFloats);
  PackNativeBonePalette(pose,floats,limit);
  for(size_t bone=0;bone<result.size();++bone) for(size_t i=0;i<kNativeBonePaletteFloats;++i)
    result[bone][i]=std::bit_cast<uint32_t>(floats[bone*kNativeBonePaletteFloats+i]);
  return result;
}
}
