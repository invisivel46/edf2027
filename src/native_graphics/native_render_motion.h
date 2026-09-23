#pragma once
// Model pose interpolation for the full-frame renderer in unlocked mode.
//
// In full-frame mode 821C9C20 never runs, so its hook's NativeModelPoseHistory
// never sees a pose. The registry keeps the history instead, per published
// pose (NativeRenderPoseMotion: the previous tick's matrices), and the models
// pass blends the poses of the items it draws at the frame's fraction with
// the same per-bone math (BlendNativePosePrepared), stateless.
#include "native_frame_motion.h"
#include "native_model_pose_history.h"
#include "native_render_entry.h"
#include <bit>
#include <cmath>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace edf::native {
// The registry's rule for one pose read at `tick` (NativeModelPoseHistory's
// rules, over published poses):
// - published / published_motion: the pose and motion the object's published
//   entry holds for this slot, null when there is none.
// - continuous: the published entry is the same generation and the pose's
//   model layout(s) did not change. A bone count change is checked here.
// - read: the tick the registry last read the object at; none when it was
//   not published since (a first read, a failed read).
// Pointer equality of pose and published means unchanged (the registry
// shares unchanged poses), and an unchanged pose keeps its motion, so an
// unchanged entry stays the same pointer. Reset (no previous) on a
// discontinuity or an unread tick (a read more than one tick after the last,
// which also covers a step jump at divisor 1). A pose that changes between
// two reads of the same tick is render-dependent: never blended, and sticky
// until a reset.
inline NativeRenderPoseMotion AdvanceNativeRenderPoseMotion(const NativeRenderPose& pose,const NativeRenderPose* published,
    const NativeRenderPoseMotion* published_motion,bool continuous,std::optional<uint64_t> read,uint64_t tick) {
  const NativeRenderPoseMotion reset{nullptr,tick,false};
  if(!published || !published_motion) return reset;
  const bool same=pose==*published;
  const bool sized=pose && *published && pose->size()==(*published)->size();
  const bool adjacent=read && (*read==tick || *read+1==tick);
  if(!continuous || !adjacent || (!same && !sized)) {
    // Already reset and unchanged: keep the published motion (and pointer).
    if(same && !published_motion->previous && !published_motion->render_dependent) return *published_motion;
    return reset;
  }
  if(same) return *published_motion;
  if(published_motion->render_dependent || *read==tick) return {nullptr,tick,true};
  return {*published,tick,false};
}

// The bound motion of `entry` (its centre, axes and radius just read at
// `tick`) against the published entry `published`, as poses advance:
// - an unchanged bound keeps the published motion;
// - a bound that moved since a read at tick-1 has that bound as previous;
// - a bound that moved since a read at this same tick (a render-only
//   iteration moved it) keeps a previous of this tick's (the tick-1 bound the
//   drawn pose may still blend from), else takes the published bound;
// - anything else (no published entry, another generation, an unread tick)
//   has none.
inline NativeRenderBoundMotion AdvanceNativeRenderBoundMotion(const NativeRenderEntry& entry,const NativeRenderEntry* published,
    bool same_generation,std::optional<uint64_t> read,uint64_t tick) {
  if(!published || !same_generation || !read || (*read!=tick && *read+1!=tick)) return {};
  const auto bits=[](float value) { return std::bit_cast<uint32_t>(value); };
  bool same=bits(entry.radius)==bits(published->radius);
  for(size_t i=0;same && i<4;++i) same=bits(entry.centre[i])==bits(published->centre[i]);
  for(size_t i=0;same && i<entry.axes.size();++i) same=bits(entry.axes[i])==bits(published->axes[i]);
  if(same) return published->bound_motion;
  if(*read==tick && published->bound_motion.valid && published->bound_motion.tick==tick) return published->bound_motion;
  return {true,tick,published->centre,published->axes,published->radius};
}

// The fraction a pose with `motion` blends at this frame, or none when the
// frame draws it as published: interpolation off, no previous pose, a pose
// of another tick than the frame's (an older one is stationary since), a
// render-dependent pose, or a fraction that yields the current pose anyway.
inline std::optional<float> NativeRenderMotionFraction(const NativeRenderPoseMotion& motion,const NativeFrameMotion& frame) {
  if(!frame.interpolate || !motion.previous || motion.render_dependent || motion.tick!=frame.tick ||
     !std::isfinite(frame.fraction) || frame.fraction>=1) return std::nullopt;
  return frame.fraction;
}

// The blend one pose draws with this frame: the previous pose and the
// fraction, or no previous pose when it draws as published (no fraction, or
// a previous pose of another size). Equal blends make equal matrices, so a
// caller may key what it derives from a pose by (pose, blend).
struct NativeRenderBlend {
  NativeRenderPose previous;
  float fraction=1;
  bool operator==(const NativeRenderBlend& other) const {
    return previous==other.previous && (!previous || std::bit_cast<uint32_t>(fraction)==std::bit_cast<uint32_t>(other.fraction));
  }
};
inline NativeRenderBlend NativeRenderBlendOf(const NativeRenderPose& pose,const NativeRenderPoseMotion& motion,const NativeFrameMotion& frame) {
  const auto fraction=NativeRenderMotionFraction(motion,frame);
  if(!fraction || !pose || motion.previous->size()!=pose->size()) return {};
  return {motion.previous,*fraction};
}

// The models pass's pose source: the matrices a drawn item uploads. The
// published pose itself (exact bits) unless NativeRenderMotionFraction gives
// a fraction; then each bone is BlendNativePosePrepared of the previous and
// current matrices, into a reused buffer valid until the next call. The
// alpha-independent half of every bone (its cut test and rotation arc) is
// kept per (previous, current) pose pair across the frames of a tick, and
// each pose's decomposition per pose, so a tick's current pose is decomposed
// once and reused as the next tick's previous: a render-only frame pays only
// the blend. Not synchronized.
class NativeRenderPoseBlender {
 public:
  struct Stats { uint64_t poses=0,blended=0,bones=0,prepared=0,reused=0,decomposed=0; };
  std::span<const NativePoseMatrix> Pose(const NativeRenderPose& pose,const NativeRenderPoseMotion& motion,const NativeFrameMotion& frame) {
    ++stats_.poses;
    if(!pose) return {};
    const auto blend=NativeRenderBlendOf(pose,motion,frame);
    if(!blend.previous) return *pose;
    const auto& row=Prepared(pose,blend.previous);
    scratch_.resize(pose->size());
    const auto& previous=*blend.previous;
    const auto& current=*pose;
    for(size_t bone=0;bone<current.size();++bone)
      scratch_[bone]=BlendNativePosePrepared(row.bones[bone],previous[bone],current[bone],blend.fraction);
    ++stats_.blended; stats_.bones+=current.size();
    return scratch_;
  }
  // One matrix of a pose (an instanced world).
  NativePoseMatrix Matrix(const NativeRenderPose& pose,const NativeRenderPoseMotion& motion,size_t index,const NativeFrameMotion& frame) {
    ++stats_.poses;
    const auto& current=pose->at(index);
    const auto blend=NativeRenderBlendOf(pose,motion,frame);
    if(!blend.previous) return current;
    const auto& row=Prepared(pose,blend.previous);
    ++stats_.blended; ++stats_.bones;
    return BlendNativePosePrepared(row.bones[index],(*blend.previous)[index],current,blend.fraction);
  }
  // Once per frame: rows unused for two frames are dropped (and their poses released).
  void EndFrame() {
    ++frame_;
    // Their vectors are kept for the next rows (a tick replaces every moving pose).
    std::erase_if(rows_,[&](auto& row) {
      if(row.second.used+2>frame_) return false;
      if(spare_blends_.size()<kSpare) spare_blends_.push_back(std::move(row.second.bones));
      return true;
    });
    std::erase_if(decomposed_,[&](auto& row) {
      if(row.second.used+2>frame_) return false;
      if(spare_bones_.size()<kSpare) spare_bones_.push_back(std::move(row.second.bones));
      return true;
    });
  }
  const Stats& stats() const { return stats_; }
  size_t size() const { return rows_.size(); }
 private:
  struct Row {
    NativeRenderPose previous,current;  // Held so the key addresses stay theirs.
    std::vector<NativePoseBlend> bones;
    uint64_t used=0;
  };
  struct Decomposed {
    NativeRenderPose pose;
    std::vector<NativePoseBone> bones;
    uint64_t used=0;
  };
  const std::vector<NativePoseBone>& Decompose(const NativeRenderPose& pose) {
    auto& row=decomposed_[pose.get()];
    row.used=frame_;
    if(row.pose!=pose) {
      row.pose=pose;
      if(row.bones.empty() && !spare_bones_.empty()) { row.bones=std::move(spare_bones_.back()); spare_bones_.pop_back(); }
      row.bones.resize(pose->size());
      for(size_t bone=0;bone<pose->size();++bone) row.bones[bone]=DecomposeNativePoseBone((*pose)[bone]);
      ++stats_.decomposed;
    }
    return row.bones;
  }
  const Row& Prepared(const NativeRenderPose& current,const NativeRenderPose& previous) {
    auto& row=rows_[current.get()];
    row.used=frame_;
    if(row.current==current && row.previous==previous) { ++stats_.reused; return row; }
    row.current=current; row.previous=previous;
    if(row.bones.empty() && !spare_blends_.empty()) { row.bones=std::move(spare_blends_.back()); spare_blends_.pop_back(); }
    // Unordered-map references stay valid across the second insertion.
    const auto& from=Decompose(previous);
    const auto& to=Decompose(current);
    row.bones.resize(current->size());
    for(size_t bone=0;bone<current->size();++bone) row.bones[bone]=PrepareNativePoseBlend(from[bone],to[bone]);
    ++stats_.prepared;
    return row;
  }
  std::unordered_map<const void*,Row> rows_;
  std::unordered_map<const void*,Decomposed> decomposed_;
  static constexpr size_t kSpare=1024;
  std::vector<std::vector<NativePoseBlend>> spare_blends_;
  std::vector<std::vector<NativePoseBone>> spare_bones_;
  std::vector<NativePoseMatrix> scratch_;
  uint64_t frame_=0;
  Stats stats_;
};
}
