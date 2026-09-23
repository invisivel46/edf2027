#pragma once
#include "native_ab_alternate.h"
#include <atomic>
#include <cstdint>

namespace edf::native {
// One switch for every cross-frame reuse of the full-frame renderer
// (edf_native_reuse_off, edf_native_reuse_off_alternate=N): a stale draw
// after something changed is a reuse bug, and a frame rendered with reuse
// off is the reference to compare it with.
//
// Contract:
// - Every reuse site asks NativeReuseAllowed() before it reuses anything
//   carried from an earlier frame (a cached resolve, a carried object, a
//   memoized lookup, a skipped read, a shared pointer standing for "unchanged")
//   or takes an output-identical shortcut (instanced uniform recording,
//   transient batching, shared effect activation, conservative cluster cull,
//   the flattened tree). When it is false the site recomputes from its inputs
//   as a first frame would, or takes the general path.
// - Rebuild, not bypass-and-forget: a reuse-off frame drops what the site
//   caches (or does not look it up) and stores what it recomputed in its
//   place, so the next reuse-on frame reuses state made from scratch one
//   frame earlier and never state the reuse-off frame skipped. Two run on the
//   side instead, leaving the kept state as it was: the static world's
//   selection cache (a reuse-off frame selects with a frame-local cache) and
//   the models pass's source memo (asked past, not validated; it validates
//   itself against the providers at the next reuse-on frame). Object ids keep
//   counting up in both modes, so no id is reused.
// - Reuse that also gates correctness keeps its correct path: the registry's
//   pose, world and layout pointers (pose motion reads "same pointer" as
//   "unchanged": a copy would reset or blend an unchanged pose), and the
//   pose-motion history itself, are never dropped.
// - The predicate is the thread's latch or the process-wide switch. The render
//   helper hook (821A5080) latches the side of the frame it renders (one
//   helper call, the latch thread_local like NativeAbSideLatch); the registry
//   tick on the engine thread and every other thread see the process-wide
//   edf_native_reuse_off only, which the bridge mirrors into
//   native_reuse_off_all. Alternation is per rendered frame, so the registry,
//   which ticks per simulation iteration, follows edf_native_reuse_off alone.
// - With both at their defaults NativeReuseAllowed() is always true.
inline std::atomic<bool> native_reuse_off_all{false};
inline thread_local bool native_reuse_off_frame=false;
inline bool NativeReuseAllowed() {
  return !native_reuse_off_frame && !native_reuse_off_all.load(std::memory_order_relaxed);
}

// The side of indexed output frame F under edf_native_reuse_off_alternate=N:
// the A/B alternation's own rule (AbSide: runs of N from the capture start
// frame, frames before it on the reference side), with reuse off on the
// reference side (logged native=0, what compare-renderer-ab-captures.py
// judges against) and reuse on on the judged side (native=1). N=1 alternates
// every frame. period 0: never off.
inline bool NativeReuseOffSide(uint64_t frame,int64_t start,int64_t period) {
  return period>0 && !AbSide(frame,start,period);
}

// Latches reuse off (or not) for one render helper call on this thread and
// restores the enclosing latch on exit.
class NativeReuseOffLatch {
 public:
  explicit NativeReuseOffLatch(bool off):previous_(native_reuse_off_frame) { native_reuse_off_frame=off; }
  ~NativeReuseOffLatch() { native_reuse_off_frame=previous_; }
  NativeReuseOffLatch(const NativeReuseOffLatch&)=delete;
  NativeReuseOffLatch& operator=(const NativeReuseOffLatch&)=delete;
 private:
  bool previous_;
};
}
