#pragma once
#include <atomic>
#include <cstdint>

namespace edf::native {
// A/B alternation between the guest path and native passes on consecutive
// output frames (edf_native_ab_alternate=N). A paused game does not re-render
// 3D, so both sides must come from live frames: frames are grouped in runs of
// N starting at the capture start frame, and odd runs take the native side.
//
// Contract:
// - The sub_821A5080 render helper hook latches the side once at entry for
//   F = indexed_output_frames+1, the indexed output frame that helper call is
//   about to produce, and logs `ab_alternate frame=F native=0|1` once per F.
//   Output captures are named <prefix>.output.<F>.bmp, so every capture maps
//   to its side through that log line (or through AbSide with the same cvars).
// - Every native pass gated by its own cvar (edf_native_static_world_pass,
//   model, post and map-effect passes, ...) must additionally require
//   NativeAbNativeSide() before taking its native branch; otherwise it takes
//   the guest branch for the whole helper call. Query the latch, never
//   recompute the side mid-frame: indexed_output_frames advances inside the
//   helper call.
// - With the cvar at 0 NativeAbNativeSide() is always true, so passes behave
//   exactly as without A/B.
// - tools/compare-renderer-ab-captures.py pairs guest frame F with native
//   frame F+1 and measures guest-vs-guest drift as the baseline.
inline bool AbSide(uint64_t frame, int64_t start, int64_t period) {
  if (period <= 0) return true;
  if (start < 0) start = 0;
  if (frame < uint64_t(start)) return false;
  return ((frame - uint64_t(start)) / uint64_t(period)) % 2 == 1;
}

inline std::atomic<bool> native_ab_native_side{true};
inline bool NativeAbNativeSide() { return native_ab_native_side.load(std::memory_order_relaxed); }

// Latches the side for one render helper call and restores the enclosing
// latch on exit, so nested or aborted helper calls cannot leak a side.
class NativeAbSideLatch {
 public:
  explicit NativeAbSideLatch(bool native)
      : previous_(native_ab_native_side.exchange(native, std::memory_order_relaxed)) {}
  ~NativeAbSideLatch() { native_ab_native_side.store(previous_, std::memory_order_relaxed); }
  NativeAbSideLatch(const NativeAbSideLatch&) = delete;
  NativeAbSideLatch& operator=(const NativeAbSideLatch&) = delete;
 private:
  bool previous_;
};
}
