#pragma once
#include "native_model_publication.h"
#include "native_model_skinning.h"
#include <array>
#include <bit>
#include <optional>
#include <vector>

namespace edf::native {
// Why one 821C9C20 object ran the original model draw instead of the native
// model pass. Reasons before Program are decided from publications alone.
enum class NativeModelPassDecline : uint32_t {
  Unpublished, Stale, Skinned, PoseTick, Pose, RenderDependent, Bone, Buffers, Scene, Pending,
  Program, Scissor, Eligibility, Geometry, PassState, World, Count
};
inline constexpr std::array<const char*,size_t(NativeModelPassDecline::Count)> kNativeModelPassDeclineNames{
  "unpublished","stale","skinned","pose_tick","pose","render_dependent","bone","buffers","scene","pending",
  "program","scissor","eligibility","geometry","pass_state","world"
};
struct NativeModelPassCounters {
  uint64_t objects=0,native=0,draws=0,passes=0,handoffs=0,replays=0;
  std::array<uint64_t,size_t(NativeModelPassDecline::Count)> fallbacks{};
  uint64_t Fallbacks() const { uint64_t sum=0; for(const auto count:fallbacks) sum+=count; return sum; }
};
// Publication gate of one rigid object at 821C9C20 entry. rigid_byte is the
// live instance+12 byte; current is NativeModelPublications::Current for the
// live container/node/vector; tick is the render's published loop tick.
struct NativeModelPoseGate {
  std::optional<NativeModelPassDecline> decline;
  const NativeModelPublications::Pose* pose=nullptr;
  explicit operator bool() const { return !decline; }
};
inline NativeModelPoseGate GateNativeModelPass(const NativeModelPublications::Layout& published,bool current,uint8_t rigid_byte,
    const NativeModelPublications::PosePublication* poses,uint64_t tick,bool render_dependent) {
  using D=NativeModelPassDecline;
  const auto decline=[](D reason) { return NativeModelPoseGate{reason,nullptr}; };
  if(!published) return decline(D::Unpublished);
  if(!current) return decline(D::Stale);
  const auto& layout=*published.layout;
  if(rigid_byte || layout.skinned) return decline(D::Skinned);
  if(!poses || poses->tick!=tick) return decline(D::PoseTick);
  const auto* pose=poses->Find(layout.instance);
  if(!pose || pose->layout_generation!=published.generation || pose->matrices.size()!=layout.bones) return decline(D::Pose);
  if(render_dependent) return decline(D::RenderDependent);
  for(const auto& mesh:layout.meshes) {
    if(!mesh.uploads_bone || mesh.bone>=pose->matrices.size()) return decline(D::Bone);
    for(const auto& batch:mesh.batches)
      if(!batch.vertex.generation || !batch.index.generation) return decline(D::Buffers);
  }
  return {std::nullopt,pose};
}
// One 821FE358 draw of the rigid path, in guest order: record, batch, pass.
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
}
