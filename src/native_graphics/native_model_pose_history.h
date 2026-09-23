#pragma once
#include "native_camera_history.h"
#include <span>
#include <vector>

namespace edf::native {
using NativePoseMatrix=std::array<float,16>;
// One bone matrix split for blending: its rows divided by their lengths (a
// rigid pose the camera's rigid math accepts), the row scales and the rigid
// part's rotation. valid: every row scale is finite and at least 1e-6 and
// NativeCameraHistory::CanInterpolate(pose,pose); an invalid bone is never
// blended.
struct NativePoseBone {
  NativeCameraPose pose;
  std::array<float,3> scale{};
  NativeCameraHistory::Quaternion rotation{};
  bool valid=false;
};
inline NativePoseBone DecomposeNativePoseBone(const NativePoseMatrix& matrix) {
  NativePoseBone bone;
  bone.pose.world=matrix; bone.pose.fov=1;
  for(size_t row=0;row<3;++row) {
    double squared=0;
    for(size_t col=0;col<3;++col) squared+=double(bone.pose.world[row*4+col])*bone.pose.world[row*4+col];
    bone.scale[row]=float(std::sqrt(squared));
    if(!std::isfinite(bone.scale[row]) || bone.scale[row]<1.e-6f) return bone;
    for(size_t col=0;col<3;++col) bone.pose.world[row*4+col]/=bone.scale[row];
  }
  if(!NativeCameraHistory::ValidPose(bone.pose)) return bone;
  bone.rotation=NativeCameraHistory::RotationOf(bone.pose.world);
  bone.valid=!NativeCameraHistory::CutWith(bone.pose,bone.pose,bone.rotation,bone.rotation);
  return bone;
}
// Whether two valid bones are too far apart to blend (a teleport, a turn past
// 90 degrees): NativeCameraHistory::CanInterpolate's cut, from the kept rotations.
inline bool NativePoseBonesCut(const NativePoseBone& a,const NativePoseBone& b) {
  return NativeCameraHistory::CutWith(a.pose,b.pose,a.rotation,b.rotation);
}
// The alpha-independent half of blending one bone from `previous` to
// `current`. blend is false when the pair must not blend: either side
// invalid, a cut between them (NativeCameraHistory::CanInterpolate), or the
// same rigid pose and scales (stationary).
struct NativePoseBlend {
  NativeCameraHistory::Blend rotation;
  std::array<float,3> previous_translation{},translation{};
  std::array<float,3> previous_scale{},scale{};
  bool blend=false;
};
inline NativePoseBlend PrepareNativePoseBlend(const NativePoseBone& previous,const NativePoseBone& current) {
  NativePoseBlend result;
  if(!previous.valid || !current.valid || NativePoseBonesCut(previous,current) ||
     (previous.pose==current.pose && previous.scale==current.scale)) return result;
  for(size_t i=0;i<3;++i) { result.previous_translation[i]=previous.pose.world[12+i]; result.translation[i]=current.pose.world[12+i]; }
  result.previous_scale=previous.scale; result.scale=current.scale;
  result.rotation=NativeCameraHistory::PrepareBlend(previous.pose,current.pose,previous.rotation,current.rotation);
  result.blend=true;
  return result;
}
// The blended bone at `fraction` from the prepared pair and the two source
// matrices. `current` exactly when the pair does not blend, fraction >= 1 or
// is not finite; `previous` exactly when fraction <= 0. Otherwise the rigid
// part follows the camera's math (NativeCameraHistory::Interpolate) and each
// row is scaled by its linearly blended scale.
inline NativePoseMatrix BlendNativePosePrepared(const NativePoseBlend& blend,const NativePoseMatrix& previous,
    const NativePoseMatrix& current,float fraction) {
  if(!blend.blend || !std::isfinite(fraction) || fraction>=1) return current;
  const float alpha=std::clamp(fraction,0.f,1.f);
  if(alpha==0) return previous;
  // NativeCameraHistory::Interpolate's world: the current rigid pose when the
  // two rigid poses are the same (only the scales move), else the arc.
  auto output=blend.rotation.same?DecomposeNativePoseBone(current).pose.world:
    NativeCameraHistory::InterpolateWorld(blend.rotation,blend.previous_translation,blend.translation,alpha);
  for(size_t row=0;row<3;++row) {
    const auto blended=blend.previous_scale[row]+alpha*(blend.scale[row]-blend.previous_scale[row]);
    for(size_t col=0;col<3;++col) output[row*4+col]*=blended;
  }
  return output;
}
// Stateless per-bone blend: what NativeModelPoseHistory::Sample outputs for a
// bone whose history holds `previous` at the tick before `current`'s. The
// full-frame models pass blends registry poses with it (native_render_motion.h).
inline NativePoseMatrix BlendNativePoseMatrix(const NativePoseMatrix& previous,const NativePoseMatrix& current,float fraction) {
  return BlendNativePosePrepared(PrepareNativePoseBlend(DecomposeNativePoseBone(previous),DecomposeNativePoseBone(current)),
    previous,current,fraction);
}

// Native copies only. Rotation/translation use the camera's rigid interpolation
// math; separate row scales preserve scaled models without shearing rotations.
// The per-bone math is BlendNativePosePrepared; this class only decides, per
// bone, which previous pose a blend starts from (or that it must not blend).
class NativeModelPoseHistory {
 public:
  void Reset() { bones_.clear(); current_input_.clear(); initialized_=false; render_dependent_=false; }
  bool render_dependent() const { return render_dependent_; }
  size_t StorageBytes() const {
    return bones_.capacity()*sizeof(Bone)+current_input_.capacity()*sizeof(NativePoseMatrix);
  }
  void Sample(std::span<const NativePoseMatrix> input,uint64_t tick,float fraction,
              std::vector<NativePoseMatrix>& output) {
    if(bones_.size()!=input.size()) { Reset(); bones_.resize(input.size()); }
    // Camera-facing attachments can already change between renders of the
    // same simulation tick. Do not add a second interpolation to those poses.
    if(initialized_ && tick==input_tick_ && !std::equal(input.begin(),input.end(),current_input_.begin()))
      render_dependent_=true;
    initialized_=true; input_tick_=tick;
    current_input_.assign(input.begin(),input.end());
    if(render_dependent_) { output=current_input_; return; }
    output.resize(input.size());
    for(size_t index=0;index<input.size();++index) {
      auto& bone=bones_[index];
      const auto current=DecomposeNativePoseBone(input[index]);
      if(!current.valid) { bone={}; output[index]=input[index]; continue; }
      const bool reset=!bone.initialized || tick<bone.tick || tick-bone.tick>1 ||
        (tick==bone.tick && (current.pose!=bone.current.pose || current.scale!=bone.current.scale)) ||
        NativePoseBonesCut(bone.current,current);
      if(reset) {
        bone.previous=current; bone.previous_input=input[index];
      } else if(tick!=bone.tick) {
        bone.previous=bone.current; bone.previous_input=bone.current_input;
      }
      bone.initialized=true; bone.tick=tick; bone.current=current; bone.current_input=input[index];
      if(reset) { output[index]=input[index]; continue; }
      output[index]=BlendNativePosePrepared(PrepareNativePoseBlend(bone.previous,bone.current),bone.previous_input,input[index],fraction);
    }
  }
 private:
  struct Bone {
    NativePoseBone previous,current;
    NativePoseMatrix previous_input{},current_input{};
    uint64_t tick=0;
    bool initialized=false;
  };
  std::vector<Bone> bones_;
  std::vector<NativePoseMatrix> current_input_;
  uint64_t input_tick_=0;
  bool initialized_=false,render_dependent_=false;
};
}
