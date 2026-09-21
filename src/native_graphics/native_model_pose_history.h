#pragma once
#include "native_camera_history.h"
#include <span>
#include <vector>

namespace edf::native {
using NativePoseMatrix=std::array<float,16>;
// Native copies only. Rotation/translation use the camera's rigid interpolation
// math; separate row scales preserve scaled models without shearing rotations.
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
      auto pose=NativeCameraPose{}; pose.world=input[index]; pose.fov=1;
      std::array<float,3> scale{};
      bool valid=true;
      for(size_t row=0;row<3;++row) {
        double squared=0;
        for(size_t col=0;col<3;++col) squared+=double(pose.world[row*4+col])*pose.world[row*4+col];
        scale[row]=float(std::sqrt(squared));
        if(!std::isfinite(scale[row]) || scale[row]<1.e-6f) { valid=false; break; }
        for(size_t col=0;col<3;++col) pose.world[row*4+col]/=scale[row];
      }
      if(!valid || !NativeCameraHistory::CanInterpolate(pose,pose)) {
        bone={}; output[index]=input[index]; continue;
      }
      bool reset=!bone.initialized || tick<bone.tick || tick-bone.tick>1 ||
        (tick==bone.tick && (pose!=bone.current || scale!=bone.scale)) ||
        !NativeCameraHistory::CanInterpolate(bone.current,pose);
      if(reset) {
        bone.rotation.Reset(); bone.previous=pose; bone.previous_scale=scale; bone.scale=scale;
      } else if(tick!=bone.tick) {
        bone.previous=bone.current; bone.previous_scale=bone.scale; bone.scale=scale;
      }
      bone.initialized=true; bone.tick=tick; bone.current=pose;
      output[index]=bone.rotation.Sample(pose,tick,fraction).world;
      if(reset || fraction>=1 || !std::isfinite(fraction) ||
         (bone.previous==pose && bone.previous_scale==scale)) {
        output[index]=input[index]; continue;
      }
      const float alpha=std::isfinite(fraction)?std::clamp(fraction,0.f,1.f):1.f;
      for(size_t row=0;row<3;++row) {
        const auto blended=bone.previous_scale[row]+alpha*(bone.scale[row]-bone.previous_scale[row]);
        for(size_t col=0;col<3;++col) output[index][row*4+col]*=blended;
      }
    }
  }
 private:
  struct Bone {
    NativeCameraHistory rotation;
    NativeCameraPose previous,current;
    std::array<float,3> previous_scale{},scale{};
    uint64_t tick=0;
    bool initialized=false;
  };
  std::vector<Bone> bones_;
  std::vector<NativePoseMatrix> current_input_;
  uint64_t input_tick_=0;
  bool initialized_=false,render_dependent_=false;
};
}
