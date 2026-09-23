#pragma once
#include <cstdint>

namespace edf::native {
// The motion budget one native frame renders at: the simulation tick and the
// fraction of the next tick elapsed (the heartbeat's NativeLoopBudget, as the
// 821A4DE8 hook published it for this render: the same budget the camera's
// 821CDDF8 interpolation used), the steps that tick dispatched, and whether
// model poses interpolate. The registry's poses for `tick` blend from their
// previous tick's at `fraction` (native_render_motion.h).
struct NativeFrameMotion {
  uint64_t tick=0;
  float fraction=1;
  uint32_t steps=0;
  bool interpolate=false;
  bool operator==(const NativeFrameMotion&) const=default;
};
// interpolate: the unlocked loop at divisor 1 with edf_native_model_interpolation,
// the condition the guest-path 821C9C20 hook blends under.
constexpr NativeFrameMotion MakeNativeFrameMotion(bool unlocked,uint32_t divisor,uint64_t tick,float fraction,uint32_t steps,
    bool model_interpolation) {
  return {tick,fraction,steps,unlocked && divisor==1 && model_interpolation};
}
}
