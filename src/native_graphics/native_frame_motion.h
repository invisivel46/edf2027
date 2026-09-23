#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace edf::native {
// The motion budget one native frame renders at: the simulation tick and the
// fraction of the next tick elapsed (the heartbeat's NativeLoopBudget, as the
// 821A4DE8 hook published it for this render: the same budget the camera's
// 821CDDF8 interpolation used), the steps that tick dispatched, and whether
// model poses interpolate. The registry's poses for `tick` blend from their
// previous tick's at `fraction` (native_render_motion.h). `unlocked`: the
// heartbeat ran the unlocked loop, where a render may dispatch no step.
struct NativeFrameMotion {
  uint64_t tick=0;
  float fraction=1;
  uint32_t steps=0;
  bool interpolate=false;
  bool unlocked=false;
  bool operator==(const NativeFrameMotion&) const=default;
};
// interpolate: the unlocked loop at divisor 1 with edf_native_model_interpolation,
// the condition the guest-path 821C9C20 hook blends under.
constexpr NativeFrameMotion MakeNativeFrameMotion(bool unlocked,uint32_t divisor,uint64_t tick,float fraction,uint32_t steps,
    bool model_interpolation) {
  return {tick,fraction,steps,unlocked && divisor==1 && model_interpolation,unlocked};
}
// Whether a render is the one its simulation tick's per-render state advances
// on. The guest advances some state once per RENDERED frame (clEffectEtc02's
// +612 lifetime in its draw, the PS_Downsample_Tone history blend); at the
// retail 60 Hz a render and a tick are 1:1, so that is once per tick. In the
// unlocked loop a render that dispatched no step (steps 0: the 821A4DE8
// transition's render_only) repeats its tick; only the first render after a
// step advances, and once however many steps it dispatched, as a locked
// render after a multi-step catch-up does. Locked renders always advance, so
// locked mode is unchanged. Call once per frame; every view and pass of that
// frame reads the one answer (NativeFrameInputs::tick_frame).
class NativeTickGate {
 public:
  bool Advance(const NativeFrameMotion& motion) {
    if(motion.unlocked && (!motion.steps || (committed_ && motion.tick==tick_))) return false;
    committed_=true; tick_=motion.tick;
    return true;
  }
  bool committed() const { return committed_; }
  uint64_t tick() const { return tick_; }
 private:
  bool committed_=false;
  uint64_t tick_=0;
};
// One guest draw that steps a counter word by a fixed `step` per call (and may
// store other words with it, `also`), made to step once per tick: `call` runs
// the original; on a held render (tick_frame false) the counter and `also` are
// put back afterwards. Returns whether it put the counter back. On a tick
// frame it is the call alone (locked mode unchanged).
//
// The simulation may write these words from another thread while the held
// render runs (the tick-side arming or reset), so every put-back is a
// compare-and-swap from the value the draw stored to the value before it,
// never a plain store: a concurrent write that lands at any point is kept.
// - The counter is put back only when it still holds exactly the draw's step
//   (before+step), atomically; otherwise another writer owns it and nothing
//   is put back.
// - Each `also` word is read right after the call (the value the draw stored)
//   and put back from exactly that value, atomically, only once the counter
//   was put back. The one write this cannot tell from the draw's own is one
//   landing between the draw's store and that read, i.e. while the draw is
//   still returning.
// Memory: Word(address) and CompareExchangeWord(address,expected,desired)
// (true when it stored desired) on guest addresses (GuestReader: an
// interlocked compare-exchange on the byte-swapped big-endian word).
template<class Memory,class Call,size_t N>
bool NativeRenderStepOncePerTick(const Memory& memory,bool tick_frame,uint32_t counter,uint32_t step,
                                 const std::array<uint32_t,N>& also,Call&& call) {
  if(tick_frame) { call(); return false; }
  const auto before=memory.Word(counter);
  std::array<uint32_t,N> saved{};
  for(size_t i=0;i<N;++i) saved[i]=memory.Word(also[i]);
  call();
  std::array<uint32_t,N> stored{};
  for(size_t i=0;i<N;++i) stored[i]=memory.Word(also[i]);
  if(!memory.CompareExchangeWord(counter,uint32_t(before+step),before)) return false;
  for(size_t i=0;i<N;++i)
    if(stored[i]!=saved[i]) memory.CompareExchangeWord(also[i],stored[i],saved[i]);
  return true;
}
// The HUD's two draw-counted advances (the HUD phase loop runs every render;
// its XUI clock and timers advance by elapsed time and are left alone):
// clGaugeRader::slot3 82176708 (+296 frames left of the damage shake, -1 per
// draw at 821768B0, with +300 the shake offset decayed at 821768AC) and the
// window cursor highlight 8218ED68 (+68 fade-in count, +1 per draw at 8218EDA8).
inline constexpr uint32_t kNativeRadarShakeFrames=296,kNativeRadarShakeOffset=300,kNativeCursorFade=68;
}
